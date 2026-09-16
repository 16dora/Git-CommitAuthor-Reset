[CmdletBinding()]
param(
    [string]$Branch = "main",
    [string]$OutputDirectory,
    [switch]$Push
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$oldName = "oldname"
$oldEmail = "xxxx@xxx.com"
$newName = "newname"
$newEmail = "yyyyy@yyy.com"
$sourceRepository = $PSScriptRoot

function Invoke-Git {
    param(
        [Parameter(Mandatory)]
        [string]$WorkingDirectory,

        [Parameter(Mandatory)]
        [string[]]$Arguments,

        [switch]$CaptureOutput
    )

    if ($CaptureOutput) {
        $output = & git -C $WorkingDirectory @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Git command failed: git -C `"$WorkingDirectory`" $($Arguments -join ' ')"
        }
        return $output
    }

    & git -C $WorkingDirectory @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Git command failed: git -C `"$WorkingDirectory`" $($Arguments -join ' ')"
    }
}

function Invoke-FilterRepo {
    param(
        [Parameter(Mandatory)]
        [string]$WorkingDirectory,

        [Parameter(Mandatory)]
        [string[]]$Arguments
    )

    Push-Location -LiteralPath $WorkingDirectory
    try {
        if ($script:filterRepoMode -eq "python") {
            & py -m git_filter_repo @Arguments
        }
        else {
            & git filter-repo @Arguments
        }
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }

    if ($exitCode -ne 0) {
        throw "git-filter-repo failed with exit code $exitCode."
    }
}

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw "Git was not found in PATH."
}

$filterRepoMode = $null
$savedErrorActionPreference = $ErrorActionPreference
$ErrorActionPreference = "SilentlyContinue"

if (Get-Command py -ErrorAction SilentlyContinue) {
    & py -m git_filter_repo --version *> $null
    if ($LASTEXITCODE -eq 0) {
        $filterRepoMode = "python"
    }
}

if ($null -eq $filterRepoMode) {
    & git filter-repo --version *> $null
    if ($LASTEXITCODE -eq 0) {
        $filterRepoMode = "git"
    }
}

$ErrorActionPreference = $savedErrorActionPreference
if ($null -eq $filterRepoMode) {
    throw "git-filter-repo is not available. Install it first, then reopen PowerShell."
}

$remoteUrl = Invoke-Git -WorkingDirectory $sourceRepository `
    -Arguments @("remote", "get-url", "origin") -CaptureOutput
$remoteUrl = ($remoteUrl | Select-Object -First 1).Trim()

if ([string]::IsNullOrWhiteSpace($remoteUrl)) {
    throw "The current repository has no origin URL."
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $parentDirectory = Split-Path -Parent $sourceRepository
    $repositoryName = Split-Path -Leaf $sourceRepository
    $timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $OutputDirectory = Join-Path $parentDirectory "$repositoryName-author-rewrite-$timestamp"
}

$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $OutputDirectory) {
    throw "Output directory already exists: $OutputDirectory"
}

Write-Host "Creating a fresh working copy at: $OutputDirectory"
$bundlePath = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("commitchange-{0}.bundle" -f [guid]::NewGuid().ToString("N"))
try {
    Invoke-Git -WorkingDirectory $sourceRepository `
        -Arguments @("bundle", "create", $bundlePath, $Branch)

    & git clone --branch $Branch --single-branch -- $bundlePath $OutputDirectory
    if ($LASTEXITCODE -ne 0) {
        throw "Failed to clone branch '$Branch' from the temporary bundle."
    }
}
finally {
    if (Test-Path -LiteralPath $bundlePath) {
        Remove-Item -LiteralPath $bundlePath -Force
    }
}

$oldBranchTip = Invoke-Git -WorkingDirectory $OutputDirectory `
    -Arguments @("rev-parse", $Branch) -CaptureOutput
$oldBranchTip = ($oldBranchTip | Select-Object -First 1).Trim()

$oldNameBytes = [System.Text.Encoding]::UTF8.GetBytes($oldName) -join ","
$oldEmailBytes = [System.Text.Encoding]::UTF8.GetBytes($oldEmail) -join ","
$newNameBytes = [System.Text.Encoding]::UTF8.GetBytes($newName) -join ","
$newEmailBytes = [System.Text.Encoding]::UTF8.GetBytes($newEmail) -join ","
$callback = @"
old_name = bytes(($oldNameBytes))
old_email = bytes(($oldEmailBytes))
new_name = bytes(($newNameBytes))
new_email = bytes(($newEmailBytes))
if commit.author_name == old_name or commit.author_email == old_email:
    commit.author_name = new_name
    commit.author_email = new_email
if commit.committer_name == old_name or commit.committer_email == old_email:
    commit.committer_name = new_name
    commit.committer_email = new_email
"@

Write-Host "Rewriting author and committer identities..."
Invoke-FilterRepo -WorkingDirectory $OutputDirectory `
    -Arguments @("--commit-callback", $callback)

Invoke-Git -WorkingDirectory $OutputDirectory `
    -Arguments @("config", "user.name", $newName)
Invoke-Git -WorkingDirectory $OutputDirectory `
    -Arguments @("config", "user.email", $newEmail)

$remotes = @(Invoke-Git -WorkingDirectory $OutputDirectory `
    -Arguments @("remote") -CaptureOutput)
if ($remotes -notcontains "origin") {
    Invoke-Git -WorkingDirectory $OutputDirectory `
        -Arguments @("remote", "add", "origin", $remoteUrl)
}

$history = @(Invoke-Git -WorkingDirectory $OutputDirectory `
    -Arguments @("log", "--all", "--format=%H`t%an`t%ae`t%cn`t%ce") -CaptureOutput)
$remaining = @($history | Where-Object {
    $_ -match [regex]::Escape($oldName) -or
    $_ -match [regex]::Escape($oldEmail)
})

if ($remaining.Count -ne 0) {
    throw "Verification failed: $($remaining.Count) commit(s) still contain the old identity. The rewritten clone was kept for inspection."
}

$newBranchTip = Invoke-Git -WorkingDirectory $OutputDirectory `
    -Arguments @("rev-parse", $Branch) -CaptureOutput
$newBranchTip = ($newBranchTip | Select-Object -First 1).Trim()

Write-Host ""
Write-Host "Rewrite and verification succeeded."
Write-Host "Old $Branch tip: $oldBranchTip"
Write-Host "New $Branch tip: $newBranchTip"
Write-Host "Rewritten clone: $OutputDirectory"

if ($Push) {
    Write-Host "Pushing rewritten history to origin/$Branch..."
    $remoteBranch = Invoke-Git -WorkingDirectory $OutputDirectory `
        -Arguments @("ls-remote", "--heads", "origin", "refs/heads/$Branch") -CaptureOutput
    $remoteBranch = @($remoteBranch | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    if ($remoteBranch.Count -ne 1) {
        throw "Could not determine the current tip of origin/$Branch. No push was performed."
    }
    $remoteBranchTip = ($remoteBranch[0] -split "\s+")[0]
    $lease = "--force-with-lease=refs/heads/${Branch}:$remoteBranchTip"
    Invoke-Git -WorkingDirectory $OutputDirectory `
        -Arguments @("push", $lease, "origin", "${Branch}:${Branch}")
    Write-Host "Remote branch origin/$Branch was updated successfully."
}
else {
    Write-Host ""
    Write-Host "No remote changes were made. Review the history with:"
    Write-Host "  git -C `"$OutputDirectory`" log --format=`"%h  %an <%ae>  |  %cn <%ce>`""
    Write-Host ""
    Write-Host "When satisfied, run this script again with -Push to rewrite the remote branch."
}

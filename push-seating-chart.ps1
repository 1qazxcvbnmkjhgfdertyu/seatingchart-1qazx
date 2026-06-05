$ErrorActionPreference = 'Stop'

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $projectRoot

Write-Host 'push-seating-chart: pushing seating-chart-app to GitHub...'

# Show a quick summary of what will go up (committed changes only)
$status = git status --short
if ($status) {
    Write-Host '--- Pending changes to push (committed) ---'
    $status | Select-Object -First 20
    if (($status | Measure-Object).Count -gt 20) {
        Write-Host '... (more)'
    }
} else {
    Write-Host '(working tree clean - pushing latest commit)'
}

$porcelain = git status --porcelain
if ($porcelain) {
    Write-Host ''
    Write-Warning 'You have uncommitted local changes. These will NOT be pushed until you commit them.'
    Write-Host 'Tip: git add . ; git commit -m "your message" ; then run push-seating-chart again'
    Write-Host ''
}

Write-Host 'Running: git push'
git push

if ($LASTEXITCODE -eq 0) {
    Write-Host ''
    Write-Host 'push-seating-chart: SUCCESS - pushed to remote.'
    $remoteUrl = git remote get-url origin 2>$null
    if ($remoteUrl) {
        Write-Host "Remote: $remoteUrl"
    }
    # Show the latest commit that was pushed
    git log -1 --oneline
} else {
    Write-Host ''
    Write-Error 'push-seating-chart: git push failed (see errors above).'
    Write-Host 'Common fixes:'
    Write-Host '  - Make sure you have authenticated (git credential manager or SSH key for this account)'
    Write-Host '  - Check the remote URL: git remote -v'
    Write-Host '  - If this is the very first push on a new clone, you may need: git push -u origin main'
    exit 1
}

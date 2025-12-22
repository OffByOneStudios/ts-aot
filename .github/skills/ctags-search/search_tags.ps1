param(
    [Parameter(Mandatory=$true)]
    [string]$Query,

    [Parameter(Mandatory=$false)]
    [switch]$Exact
)

$tagsFile = Join-Path (Get-Location) "tags"
if (-not (Test-Path $tagsFile)) {
    Write-Error "Tags file not found at $tagsFile. Please run 'update tags' task first."
    exit 1
}

# Use Select-String to find matches. 
# For exact matches, we look for the query at the start of the line followed by a tab.
if ($Exact) {
    $pattern = "^$([regex]::Escape($Query))`t"
} else {
    $pattern = [regex]::Escape($Query)
}

$matches = Select-String -Path $tagsFile -Pattern $pattern | Select-Object -First 50

if (-not $matches) {
    Write-Host "No matches found for '$Query'."
    exit 0
}

foreach ($match in $matches) {
    $line = $match.Line
    $parts = $line -split "`t"
    if ($parts.Length -ge 3) {
        $tagName = $parts[0]
        $fileName = $parts[1]
        # The search pattern often contains /.../ or ?...?
        $searchPattern = $parts[2] -replace '^/','' -replace '/;"$','' -replace '^\?','' -replace '\?;"$',''
        
        Write-Host "Symbol: $tagName"
        Write-Host "File:   $fileName"
        Write-Host "Match:  $searchPattern"
        Write-Host "----------------------------------------"
    }
}

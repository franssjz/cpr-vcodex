param(
  [string]$Environment = "default",
  [int]$Jobs = 1
)

$ErrorActionPreference = "Stop"
$env:PYTHONIOENCODING = "utf-8"
$env:PYTHONUTF8 = "1"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

python -X utf8 -m platformio run -e $Environment -j $Jobs
exit $LASTEXITCODE

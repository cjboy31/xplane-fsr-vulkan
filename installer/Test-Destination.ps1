param([Parameter(Mandatory=$true)][string]$Nsis)
$ErrorActionPreference='Stop'
# Exercise NSIS destination precedence using the production directives, without
# touching the product's registry key or any real application installation.
$source=Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'Setup.nsi')
$directory=[regex]::Match($source,'(?m)^InstallDir "(.+)"\r?$')
$registry=[regex]::Match($source,'(?m)^InstallDirRegKey HKCU "([^"]+)" "InstallLocation"\r?$')
if(-not $directory.Success -or -not $registry.Success){throw 'Installer must use native NSIS destination precedence'}
$id=[guid]::NewGuid().ToString('N')
$fixture=Join-Path $PSScriptRoot ('test-destination-'+$id)
$key='Software\XPFSR24\DestinationTests\'+$id
$registryPath='HKCU:\'+$key
New-Item -ItemType Directory -Path $fixture|Out-Null
$default=Join-Path $fixture 'default app'
$previous=Join-Path $fixture 'previous app'
$explicit=Join-Path $fixture 'explicit app'
$result=Join-Path $fixture 'result.txt'
$executable=Join-Path $fixture 'probe.exe'
$script=Join-Path $fixture 'probe.nsi'
$directives=$directory.Value.Replace($directory.Groups[1].Value,$default)+"`n"+$registry.Value.Replace($registry.Groups[1].Value,$key)
$probe=@"
Unicode true
RequestExecutionLevel user
OutFile "$executable"
$directives
Section
 FileOpen `$0 "$result" w
 FileWrite `$0 `$INSTDIR
 FileClose `$0
SectionEnd
"@
[IO.File]::WriteAllText($script,$probe,[Text.UTF8Encoding]::new($false))
& $Nsis /V2 $script
if($LASTEXITCODE -ne 0){throw 'Destination fixture compilation failed'}
function Check-Destination([string]$Arguments,[string]$Expected){
 $process=Start-Process -FilePath $executable -ArgumentList $Arguments -WindowStyle Hidden -PassThru -Wait
 if($process.ExitCode -ne 0 -or [IO.File]::ReadAllText($result) -ne $Expected){throw "Destination precedence failed: $Arguments"}
}
try{
 Check-Destination '/S' $default
 New-Item -Path $registryPath -Force|Out-Null
 New-ItemProperty -LiteralPath $registryPath -Name InstallLocation -Value $previous -PropertyType String|Out-Null
 Check-Destination '/S' $previous
 # Regression: /D is consumed before .onInit, even when it names the default.
 Check-Destination ('/S /D='+$default) $default
 Check-Destination ('/S /D='+$explicit) $explicit
 Write-Output 'PASS: fresh, registered, explicit-default and explicit-other NSIS destinations.'
}finally{
 if(Test-Path -LiteralPath $registryPath){Remove-Item -LiteralPath $registryPath}
}

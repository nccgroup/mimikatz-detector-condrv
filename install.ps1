function Install-Driver($name)
{
	$cleanName = $name -replace ".sys|.\\", ""

	sc.exe stop $cleanName
	sc.exe delete $cleanName

	cp $name c:\windows\system32\drivers\ -verbose -force
	wevtutil im ..\..\Mimikatz_Console_Detection\event.xml
	sc.exe create $cleanName type= kernel start= boot error= normal binPath= c:\windows\System32\Drivers\$cleanName.sys DisplayName= $cleanName

	sc.exe start $cleanName
}

function Uninstall-Driver($name)
{
	$cleanName = $name -replace ".sys|.\\", ""
	wevtutil um ..\..\Mimikatz_Console_Detection\event.xml
	sc.exe stop $cleanName
	sc.exe delete $cleanName
}
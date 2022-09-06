# Mimikatz Detector - ConDrv + ConsoleMonitor

This project is based on the research that was done for mimikatz detection technique phase 2.
It has multiple projects in the solution:

* ConsoleMonitor - A GUI application in C# to sniff console related traffic (input output through ConDrv)
* ConsoleMonitorDriver - A KMDF filter driver that sniffs and transmits the data to ConsoleMonitor
* Mimikatz_Console_Detection - Similar to [Mimikatz Detector - Busylight](https://github.com/nccgroup/mimikatz-detector-busylight), this sends events to the event log when mimikatz was run.

## ConsoleMonitor - ConsoleMonitorDriver

The Console Monitor Driver is a KMDF kernel-mode filter driver that captures certain Fast I/O operations (input and output) that is sent to or from the ConDrv. ConDrv is a device created by condrv.sys, which handles the traffic between the Console Application (cmd/powershell/etc) and the actual console (conhost.exe). Console Monitor is a C# GUI application that shows the enduser every keystroke or line that was sent to or from the console. The use of this application is negligible, but it is great for presentations and debugging.

The Console Monitor GUI application creates a named pipe and listens for incoming connections. All data that is sent to the namedpipe is shown on the GUI. The driver checks if the namedpipe is in place, if not, it will return an error and will not install. In case the namedpipe is there (GUI is running), it installs and sends everything that is sniffed to the pipe.

The following steps needs to be followed to make it work:
* Compile both projects
* Execute ConsoleMonitor binary
* Execute install.ps1's Install-Driver function to install the ConsoleMonitorDriver.sys

## Mimikatz_Console_Detection

Underlying technique is the same as before with the ConsoleMonitorDriver, but here instead of sniffing for all data, the KMDF filter driver tries to match several strings to the data passing over the driver. In this particular case, the title of the console application is checked against a static string, also the content of the console output is checked against multiple static strings. In case any of the matches are happening, an event is sent to the event log through Event Tracing for Windows (ETW). At the moment, 4 different events are defined in the driver:

* Driver loaded (event id 1)
* Title Matched (event id 2)
* Text Matched (event id 3)
* Driver unloaded (event id 4)

To prepare the system to be able to read these messages properly the event.xml file needs to be installed with (wevtutil.exe), but this is covered in the install.ps1 script.

Steps to make the driver work:
* Compile the code
* Use the install.ps1's Install-Driver script to install the event.xml with wevtutil
* Also to install the driver
* Check the event log after mimikatz was executed.

Obviously the strings that are matched against the title and the output can be changed so other malware/tools can be detected with the tool if the strings are changed.


## Uninstallation

Just use the Uninstall-Driver function from install.ps1.

## Known limitations

Redirects to files will not be detected, since the output will not be sent to the ConDrv device.
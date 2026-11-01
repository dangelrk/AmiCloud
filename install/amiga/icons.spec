# Workbench-Icons der Installationsschublade (make dist-amiga), Format
# siehe core/tools/mkicon.py. Positionen: Mitte des Icons minus 18.
.                          drawer  drawer    win=60,30,520,220
Install-AmiCloud           project install   tool=Installer tt=APPNAME=AmiCloud tt=MINUSER=AVERAGE tt=DEFUSER=AVERAGE pos=62,6
Uninstall-AmiCloud         project uninstall tool=C:IconX tt=WINDOW=CON:0/20/640/200/Uninstall-AmiCloud/CLOSE/WAIT pos=232,6
ReadMe                     project doc       tool=SYS:Utilities/MultiView pos=372,6
Docs                       drawer  drawer    win=80,50,300,100 pos=42,70
C                          drawer  drawer    win=80,50,360,100 pos=132,70
Examples                   drawer  drawer    win=80,50,380,100 pos=222,70
Server                     drawer  drawer    win=80,50,360,100 pos=422,70
Storage                    drawer  drawer    win=80,50,300,100 pos=322,70
LICENSE                    project doc       tool=SYS:Utilities/MultiView pos=42,132
THIRD-PARTY-NOTICES        project doc       tool=SYS:Utilities/MultiView pos=192,132
Install-AmiCloud.shell     project doc       tool=C:IconX tt=WINDOW=CON:0/20/640/300/Install-AmiCloud/CLOSE/WAIT pos=362,132

Docs/AmiCloud.guide        project guide     tool=SYS:Utilities/MultiView pos=72,6
C/amicloud-commander       tool    cloud     stack=32768 pos=62,6
C/AmiCloudPrefs            tool    prefs     stack=32768 pos=222,6
Examples/amicloud.cfg.example project doc    tool=SYS:Utilities/MultiView pos=82,6
Examples/AMICLOUD.mountlist project doc      tool=SYS:Utilities/MultiView pos=232,6
Storage/DOSDrivers         drawer  drawer    win=80,50,300,100 pos=82,6
Storage/DOSDrivers/AMICLOUD project mount    tool=C:Mount pos=82,6
Server/ReadMe              project doc       tool=SYS:Utilities/MultiView pos=62,6

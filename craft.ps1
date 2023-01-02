
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned

iex ((new-object net.webclient).DownloadString('https://raw.githubusercontent.com/KDE/craft/master/setup/install_craft.ps1'))


Set-ExecutionPolicy -Scope CurrentUser RemoteSigned -Force

iex ((new-object net.webclient).DownloadString('https://raw.githubusercontent.com/KDE/craft/master/setup/install_craft.ps1'))

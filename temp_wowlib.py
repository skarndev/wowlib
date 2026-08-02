import wowlib
from typing import reveal_type

from wowlib import ClientVersion
from wowlib.fs import FileSystemSettings

if __name__ == '__main__':
    a = wowlib.db.tables.AnimationData.for_version(wowlib.Expansion.Wotlk)
    reveal_type(a)

    with wowlib.fs.FileSystem.open(FileSystemSettings(
        client_path="/Users/skarn/WoWModding/Clients/World of Warcraft 3.3.5a/",
        locale=wowlib.Locale.enUS,
        version=wowlib.to_client_version(wowlib.Expansion.Wotlk)
    )) as fs:
        fs: wowlib.fs.FileSystem
        data = fs.read_file('DBFilesClient/AnimationData.dbc')
        a.read(data)
        for rec in a.records:
            print(rec.name)


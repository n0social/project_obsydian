# Warden challenge tables (.cr)

Precomputed HASH_REQUEST reply tables from the public VMaNGOS warden_modules pack:
https://github.com/vmangos/warden_modules

Required for RetroWoW / VMaNGOS classic Warden. The client matches the module MD5
from MODULE_USE to `<md5>.cr` and replies with the CR entry for the server seed.
Do not invent HASH_RESULT hashes — the server memcmp's them.

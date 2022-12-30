Import("env")

upload_port = env.GetProjectOption("upload_port")
keyfile = "publickey.pub"
tmpfile = "publickey.b64"

env.AddCustomTarget(
    name="write_publickey",
    dependencies=None,
    actions=[
        "cat " + keyfile + " | python -m base64 -d - > " + tmpfile,
        "esptool.py --after no_reset --port " + upload_port + " erase_region 0xe000 0x1000",
        "esptool.py --before no_reset --port " + upload_port + " write_flash 0xe000 " + tmpfile,
        "rm " + tmpfile
    ],
    title="Write Public Key",
    description="Writes the OpenHaystack public key to the target board (erase first)"
)
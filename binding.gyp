{
  "targets": [
        {
          "target_name": "fuse4js",
          "sources": [ "fuse4js.cc" ],
          "include_dirs": [
             '<!@(pkg-config fuse --cflags-only-I | sed s/-I//g)'
          ],
          "link_settings": {
            "libraries": [
              '<!@(pkg-config --libs-only-l fuse)'
            ]
          }
        }
      ]
}

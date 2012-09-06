{
  'conditions': [
    ['OS=="linux"', {
      "targets": [
        {
          "target_name": "fuse4js",
          "sources": [ "fuse4js.cc" ],
          "link_settings": {
            "libraries": [
              "/lib/libfuse.so.2"
            ]
          }
        }
      ]
    }]
  ]
}

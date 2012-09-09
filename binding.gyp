{
  'conditions': [
    ['OS=="linux"', {
      "targets": [
        {
          "target_name": "fuse4js",
          "sources": [ "fuse4js.cc" ],
          "link_settings": {
            "libraries": [
              '<!@(pkg-config --libs-only-l fuse)'
            ]
          }
        }
      ]
    }]
  ]
}

{
  "targets": [
    {
      "target_name": "libchunk",
      "sources": ["src/binding.cc"],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "../include"
      ],
      "libraries": [
        "<(module_root_dir)/../build/libchunk.a"
      ],
      "cflags_cc": [
        "-std=c++17"
      ],
      "defines": ["NAPI_DISABLE_CPP_EXCEPTIONS"],
      "conditions": [
        [
          "OS=='linux'",
          {
            "libraries+": [
              "<!@(pkg-config --libs libavif 2>/dev/null || echo '-lavif')",
              "-lz",
              "-lm",
              "-lpthread"
            ]
          }
        ],
        [
          "OS=='mac'",
          {
            "libraries+": [
              "<!@(pkg-config --libs libavif 2>/dev/null || echo '-lavif')",
              "-lz"
            ]
          }
        ]
      ]
    }
  ]
}

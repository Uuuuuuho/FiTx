# Scripts

## SuperC preprocess helper

`06_superc_preprocess.sh` runs SuperC on a single file or an entire directory and writes the preprocessor output to an output directory. It defaults to `-E` (preprocess).

### Examples

```bash
./scripts/06_superc_preprocess.sh superc/fonda/cpp_testsuite/preprocessor/accept.c
```

```bash
./scripts/06_superc_preprocess.sh --exts c,h superc/fonda/cpp_testsuite/preprocessor
```

```bash
./scripts/06_superc_preprocess.sh --superc-args "-E -I /path/include" superc/fonda/cpp_testsuite/preprocessor
```

### Useful options

- `--output-dir`: change output directory (default: `out/superc_preprocessed`)
- `--suffix`: change output suffix (default: `_superc.c`)
- `--exts`: comma-separated extensions for directory mode (default: `c`)
- `--superc-args`: pass SuperC flags like `-E`, `-D`, `-I`
- `--java-cmd`, `--java-args`, `--java-dev-root`: override Java or SuperC paths

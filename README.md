# multigrub

UEFI x86_64向けの最小GRUBマルチブート試作です。ISOをDATAに保存し、GRUBで選択してFAT32のSCRATCHへ展開した後、そのISOの `EFI/BOOT/BOOTX64.EFI` を既存GRUB chainloaderで起動します。

ISOの読み取りにはGRUBのloopbackとfilesystem API、scratchへの書き込みにはUEFI Simple File System / File Protocolを使用します。loopbackはGRUB内での読み取りだけに使用します。Linuxのkernel、initramfs、kernel command lineは変更しません。

Alpine 3.22.3 standardの公式ISOで、展開したFAT32から変更のないEFI・kernel・initramfsを経由してLinuxのログインまで確認しました。今回の追加チェックと実ISO一覧は [互換性レポート](docs/compatibility.md)。詳しい実行結果は [検証記録](docs/validation.md)、設計調査は [design.md](docs/design.md) を参照してください。

## 開発と自動テスト

Debian trixie、x86_64 Linuxを想定しています。基本ツールとしてGCC、binutils、make、Python 3、curl、tar、dpkgとDebian archive keyringが必要です。

```sh
make deps       # 署名検証されたDebianパッケージを .build/tools に展開（root不要）
make build      # SHA-256固定のGRUB 2.12ソースをビルドし、独自モジュールを組み込む
make test       # GPTイメージ生成、QEMU/OVMF実行、シリアル・ホスト側検証
make test-compat # 展開前チェック、symlink拒否、エラー分類、長いラベル
make test-linux # 公式Alpine ISO取得、通常接続・USB接続でLinux起動検証
```

必要な追加パッケージは `qemu-system-x86 ovmf xorriso mtools gdisk dosfstools e2fsprogs bison flex gawk gnu-efi tesseract-ocr tesseract-ocr-eng` です。通常のパッケージ管理で導入済みなら `make deps` を省略できます。`scripts/env.sh` はローカル導入先を優先します。

GRUBソース・ツール・ディスク・ログは `.build/` に作成します。初回はGRUB全体を4並列でビルドし、その後は変更された独自モジュールと依存情報を更新します。並列数は `JOBS=2 make build` のように指定できます。ホストのmount、loop device、KVM、GUI操作は不要です。

自動テストは実際のQEMU guestを起動し、書き込み後のディスクをホストのmtoolsで検証します。ログは `.build/test-run.log`（出力を保存した場合）、`.build/logs/*.log`、ビルドログは `.build/build.log` と `.build/module-build.log` です。

## ISOを選択して起動する

```sh
source scripts/env.sh
python3 scripts/image.py --config grub/grub.cfg --iso /path/to/linux.iso --name demo
scripts/run.sh .build/demo/disk.img
# USB Mass Storage接続
scripts/run.sh .build/demo/disk.img --usb
```

シリアル端末にISOのメニューが出ます。矢印キーとEnterで選択できます。QEMU終了はCtrl-a、xです。ISOは複数の `--iso` で追加できます。同名のISOは指定しないでください。選択したISOの展開時に、専用scratchの以前の内容を削除します。

`image.py` は指定した `.build/NAME` の生成イメージを作り直します。`--reuse` は既存イメージのESPのGRUBだけを更新し、DATAとSCRATCHを保持します。稼働中のVMが使用しているイメージを変更しないでください。

標準の構成は次のとおりです（3GiBのsparseファイル）。

| GPT partition | 内容 | サイズ |
|---|---|---|
| p1 EFI | FAT32、独自GRUB `EFI/BOOT/BOOTX64.EFI` | 64MiB |
| p2 DATA | ext4、`/iso/*.iso` | 1GiB |
| p3 SCRATCH | FAT32、ISO展開先 | 1.5GiB |

DATAは `mkfs.ext4 -O ^extent,^64bit` で作成します。GRUB 2.12のext4 readerは先頭に穴のあるextentファイルでエラーになるため、従来のブロック配置を使います。ISO全体のハッシュもこの配置で検証します。容量は `--data-mib 8192 --scratch-mib 8192` のように指定できます。生成先の `partitions.json` に各パーティションのoffsetと容量を記録します。従来のホスト検証スクリプトは既定のscratch offsetを使用するため、カスタム容量ではこのmetadataに従って検証してください。

## GRUBコマンド

`insmod hello` で独自モジュールを読み込みます。初期試作では上流の `hello` ビルドルールを利用しています。上流ソースへの適用は `.build/` 内だけで、GRUB coreの変更はありません。GRUBと結合するCモジュールはGPL-3.0-or-laterです。

```text
hello_test
write_test
extract_file /iso/test.iso /README.TXT /README.TXT
extract_iso /iso/test.iso
chain_scratch
bootiso_boot /iso/test.iso
bootiso_menu /iso
```

ISOの相対デバイスはGRUBの `root` です。通常は `search --label DATA --set=root` を先に実行します。`chain_scratch` はscratchを検出し、EFIが存在することを確認してから、`root` をscratchに設定して既存chainloader/bootを呼びます。EFIが戻った場合は元のrootを復元します。

scratchは `/.multigrub-scratch` の正確な内容で識別します。マーカーを持つ複数のfilesystemがある場合は書き込みを拒否します。ファイルシステムのラベルには依存しないため、ISOのVolume IDを反映した後も識別できます。

`extract_iso` は入力ファイルと容量を事前検査し、scratchを清掃して展開します。FATで表現できない名前、case collision、4GiB超の単一ファイル、深さ64超、10万項目超は失敗として扱います。POSIX属性は再現しません。ISO上のRock Ridge symlinkは展開前に検出し、通常ファイルへのリンクも含めて `SYMLINK_UNSUPPORTED` として拒否します。

展開成功時、EFIが存在する場合にだけ `/.bootiso-cache` を最後にflushして保存します。記録にはISOパス・サイズ・利用可能な更新日時・ISO全体のSHA-256が含まれます。キャッシュ一致時は再展開を省略しますが、変更検出のためISO全体の読み取りは毎回必要です。scratchを外部から改変した場合はキャッシュを削除してください。`write_test` と `extract_file` はキャッシュを無効化します。

FATで**完全に表現できる**1〜11文字のVolume IDはUEFI `SetInfo` で反映します。長いID、大小文字が変わるID、禁止文字を含むIDは理由を表示して反映を省略します。ラベルの切り詰めはしません。

## Linux ISOの自動検証

`make test-linux` は公式Alpine 3.22.3 standard ISOを取得し、公式checksumと固定SHA-256を照合します。新しいディスクで全体を展開して通常接続で起動し、元の120ファイルとバイト単位で比較した後、USB Mass Storage接続でキャッシュから起動します。

QEMUは1GiB RAMとVGAを備え、画面をTesseractで読み、QMPで標準のrootログインを行います。guest内の `uname`、`/proc/cmdline`、`mount` の結果をシリアルへ出力し、Linux kernel・user space・元のkernel引数・FAT32 scratchの読み取り専用mountを検証します。EFI、kernel、initramfs、設定ファイルは変更しません。ログは `.build/linux-full-test.log`（出力を保存した場合）、`.build/linux-sata-test.log`、`.build/linux-usb-test.log`、画面とシリアルは `.build/linux-validation/linux-{sata,usb}/` に保存します。

AlpineのEFI内の初期設定は元のVolume ID `alpine-std 3.22.3 x86_64` を検索します。このIDはFATの11文字制限に収まらず、反映を省略するためラベル検索の警告が出ます。それでもLoadedImageのデバイスから引き継いだscratchのrootで元の設定を読み、起動できます。この結果は当該Alpine ISOの検証であり、他のディストリビューションの起動を保証しません。

対象外はSecure Boot、BIOS/CSM、ARM、Windows、persistence、暗号化filesystem、network bootです。UDF reader自体は組み込まれていますが、symlink検査が未実装のため全体展開では明示的にUNSUPPORTEDとします。

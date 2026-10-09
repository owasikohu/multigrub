# Generic extraction互換性レポート

追加8本（非Linuxを含む）の結果と比較起動は [iso-matrix.md](iso-matrix.md) を参照してください。下記のDebian Live/Ubuntu 24.04と、新たに試したmini ISOは別の媒体です。

方式はISO → FAT32 scratchへ全ファイル展開 → scratchの `/EFI/BOOT/BOOTX64.EFI` を既存chainloaderで起動、のままです。GRUB coreへのpatch、ディストリ固有handler、kernel引数変更、initramfs hook、ISO用の仮想CD-ROM・仮想Block Deviceは追加していません。

## 実装済み機能

- EFI loaderの存在・読み取り可否・非空を展開前に確認。Rock Ridgeの名前はFATと異なりcase sensitiveなので、パスはFATと同じcase-insensitive照合で検出し、曖昧なcase collisionは拒否。欠落時は `NO_EFI_LOADER`。
- GRUB filesystem APIで全ファイルを開き、サイズが `0xffffffff` を超えたら `FILE_TOO_LARGE` とパス・十進サイズを表示。全ファイルのpreflightが成功するまでscratchを清掃しません。
- GRUBのdirhookがsymlink型を公開しないため、ISO9660のSystem Use Areaを追加で検査。Rock RidgeのSLとCE continuationを検出し、通常ファイルへのリンクも `SYMLINK_UNSUPPORTED` として拒否します。リンク先の追跡・通常ファイル化は行いません。既存のGRUB readerによる展開は維持しています。
- Volume Labelはbest-effort。表現不能、Volume IDの読み取り失敗、UEFI SetInfo/flush失敗は警告を表示して続行します。ラベルの切り詰めは行いません。
- 読み取り・容量・FAT書き込み等の失敗は `EXTRACTION_FAILED`、EFIロード失敗は `CHAINLOAD_FAILED`、実行したEFIが戻った場合は `BOOTLOADER_RETURNED`。戻りのEFI statusが失敗であっても、起動後に戻った段階として記録します。
- 標準GRUB設定と追加互換性テストは `terminal_output console serial` で両方へ診断を出力。カスタム設定も両端末を有効にしてください。引き継ぎ後のstock bootloader/kernelのコンソール設定は変更しません。
- preflightを通っていない旧キャッシュを再利用しないようcache形式をv2へ更新。従来のハッシュ検証は維持します。
- 大きなISOの検証に備え、イメージ生成に `--data-mib` / `--scratch-mib` を追加。既定サイズは維持し、offsetは生成先の `partitions.json` に記録します。

## QEMUで確認済み機能

`make test` は従来のGRUB起動、UEFI FAT書き込み、全ファイル展開、EFI引き継ぎ、ラベル、メニュー、cache、USB、collision/traversal拒否を実行します。

`make test-compat` はEFI欠落、通常ファイル/ディレクトリ/リンク切れ/循環/continuationを伴うsymlink、4GiB超の宣言サイズ、不正EFI、戻るEFI、長いVolume ID、小文字のEFIパスを検証します。preflight拒否時には既存scratchのsentinelが残り、展開ログと成功cacheが生成されないことを確認します。巨大ファイルのテストは小さい人工ISOのmulti-extent metadataに4294967296 bytesを宣言したものです。実際の4GiB payloadをコピーした試験ではありません。

`NO_EFI_LOADER` はQEMUのserial logとVGA画面のOCRの両方で検証済みです。小文字EFIパス、長いVolume ID、戻るEFIのケースも実際にQEMU/OVMFで実行しました。

Volume IDの長さ制限による省略は起動試験済みです。firmwareのSetInfo/flush失敗はコード上で非致命的に扱いますが、実際にその失敗を返すfirmwareでの試験は未実施です。

## 実ISOの互換性一覧

2026-10-09のこの環境での記録です。**FAIL/DOWNLOADは取得段階の失敗であり、ISOの非互換性を判定した結果ではありません。** 対象バージョンを取得できなかった行は互換性UNKNOWNです。取得不能をUNSUPPORTEDにはしません。

| 系統 | ISO | 結果 | 最後に確認した段階・理由 |
|---|---|---|---|
| Alpine Linux | standard 3.22.3 x86_64 | PASS | 変更後の最終テストでもSATA/USBともstock EFI → stock GRUB → Linux 6.12.67-0-lts → rootログイン。全120ファイルのバイト一致、元のcmdline、FAT mount、USB cache hitを確認 |
| Arch Linux | 未取得 | FAIL / DOWNLOAD、互換性UNKNOWN | `geo.mirror.pkgbuild.com/iso/latest/` へのHTTPS CONNECTが403 |
| Debian Live | 未取得 | FAIL / DOWNLOAD、互換性UNKNOWN | `cdimage.debian.org/debian-cd/current-live/amd64/iso-hybrid/` へのHTTPS CONNECTが403 |
| Ubuntu | 未取得 | FAIL / DOWNLOAD、互換性UNKNOWN | `releases.ubuntu.com/24.04/` へのHTTPS CONNECTが403 |
| Fedora | 未取得 | FAIL / DOWNLOAD、互換性UNKNOWN | `download.fedoraproject.org/pub/fedora/linux/releases/` へのHTTPS CONNECTが403 |
| openSUSE | 未取得 | FAIL / DOWNLOAD、互換性UNKNOWN | `download.opensuse.org/distribution/leap/` へのHTTPS CONNECTが403 |
| FreeBSD | 未取得 | FAIL / DOWNLOAD、互換性UNKNOWN | `download.freebsd.org/releases/amd64/amd64/ISO-IMAGES/` へのHTTPS CONNECTが403 |

最終 `make test-linux` は終了コード0で成功しました。ログは `.build/compat-alpine-final.log`、guestのserial/画面は `.build/linux-validation/linux-{sata,usb}/`。

取得診断は `.build/real-iso/downloads.json`。Alpine SHA-256は `4e05fdcf5d0cc8e7bd404d4512884bcce5f40f046f4adecbc84b06b83477cd1d`。`make test-linux` は新規ディスクで全128項目（120ファイル・8ディレクトリ）を展開し、全ファイルのバイト一致を検証したうえで、SATA/USBのlogged-in user spaceからuname・元のcmdline・FAT scratchの読み取り専用mountを確認します。通常接続は `/media/sda3`、USBは `/media/usb` です。長いAlpine Volume IDの検索警告が出ても、引き継いだscratch rootでstock設定を読み込めます。

ほかのISOを取得できた場合、十分なDATA/SCRATCH容量を指定してテストしてください。例えば:

```sh
source scripts/env.sh
python3 scripts/image.py --config grub/grub.cfg --iso /absolute/path/linux.iso \
  --name real-iso --data-mib 8192 --scratch-mib 8192
scripts/run.sh .build/real-iso/disk.img
```

ここでの起動はシリアル端末です。stock OSがVGAだけを使う場合、QMPの画面保存またはVNC等でguest画面を観測し、Live/Installer環境の起動まで確認する必要があります。展開成功やkernel文字列だけでPASSにはしません。

## 既知の制限

- FATの名前・大小文字・4GiB単一ファイル制限、POSIX mode/ownership等の再現不可。case collisionは展開中に拒否し、scratchは途中まで変更され得ます。
- 全symlinkを保守的に拒否します。ISOによってはbootと無関係なリンクでもUNSUPPORTEDになります。UDFのsymlink型を安全に検査する機構は未実装のため、UDFは `SYMLINK_UNSUPPORTED` とします。
- ラベルが不要というのはこの実装の条件です。ISO付属bootloaderやinitramfs自身がISO9660ラベル、光学媒体、ISO9660 filesystem等を必須としていれば起動できない場合があります。OS固有hackで補いません。
- Live環境内の圧縮filesystemに入っているsymlinkはISO上のsymlinkではないため、このチェックでは拒否しません。その圧縮イメージ自体は変更しません。
- 合計容量、深さ64、10万項目、4096文字path等の制限は維持。scratchの外部改変はcacheで検出できません。
- Secure Boot、BIOS、ARM、Windows、persistence等は対象外。stock EFIの存在はEFIイメージの正常性や後続のOS起動を保証しません。

## 実機で確認すべき項目

- 各UEFI firmwareによるGPT/FAT32認識、File Protocolの書き込み・flush・削除・SetInfoの挙動。
- USB 2/3、機種ごとのDeviceHandle/chainload、cold bootと再起動。
- Live/Installerまで到達すること、boot mediaをscratchとして正しく認識すること、元のkernel引数。
- 不足容量・途中のI/O失敗・電源断後の再展開とcache再作成。
- シリアル端末がない機器での画面診断。Secure Bootは無効の条件で確認。

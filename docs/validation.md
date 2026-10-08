# 実行・検証記録

現在の環境で実際に実行した結果を記録します。一般的なLinux ISOの起動成功を、検証用EFIアプリの成功から推定しません。

| Milestone | 現在の結果 |
|---|---|
| 1 | GRUB 2.12をソースビルドし、QEMU 10.0.13 / OVMF 2025.02で `GRUB started successfully` を確認 |
| 2 | 独自モジュールの `hello_test` が期待したメッセージを出力 |
| 3 | UEFI filesystem protocolでscratchへ `/test.txt` を書き込み、ホストで内容一致とESP非変更を確認 |
| 4 | GRUBのISO9660 readerからREADMEをコピーし、ホストで内容一致を確認 |
| 5 | 入れ子・空ディレクトリ・バイナリを含む10項目を再帰展開し、全ファイルのバイト一致と旧scratchファイル削除を確認 |
| 6 | 既存EFI chainloaderでISO内のEFIアプリを起動。そのアプリがLoadedImage.DeviceHandleのfilesystemからscratch上のpayloadを読み、成功メッセージを出力 |
| 7 | Alpine 3.22.3 standardの公式ISOを展開し、変更なしのEFI・kernel・initramfsでLinux 6.12.67-0-ltsとrootログインを確認。通常接続・USB接続とも成功。元の120ファイルはすべてバイト一致 |
| 8 | ISO Volume ID `MULTIGRUB` をUEFI SetInfoで反映し、ホストのmlabelで完全一致を確認 |
| 9 | ISOファイルのメニュー生成・自動選択・展開・EFI引き継ぎを確認 |
| 10 | 統合テスト成功。キャッシュ再利用、同じパス・サイズ・mtimeで中身のみ変えたISOの再展開、失敗時の成功cache削除を確認 |

## 診断して修正した問題

- GRUB配布ソースの空 `extra_deps.lst` が欠けていたため、上流ChangeLogに従い空ファイルを生成。
- GRUBの依存生成は `asorti` を使うため、mawkではなくGNU awkを使用。
- root不要のDebian package download/extract方式でQEMU、OVMFなどを導入。APT署名とpackage checksumの検証は維持。
- GRUB 2.12のchainloaderはファイルの指定デバイスではなく `root` をLoadedImageのDeviceHandleに使用する。呼び出し前にscratchへrootを切り替え、元のrootを必要に応じて復元。
- UEFI経由のFAT書き込みはGRUBのdisk cacheを更新しないため、入力ファイルを閉じた後で公開cache tableの未ロック項目を無効化。処理は固定GRUB 2.12の既存cache invalidationと同じ。coreへのpatchは不要。
- 新しいgnu-efiのlinker scriptは `.rodata` を独立sectionにするため、検証用EFIのPE生成にそのsectionを含める。
- GRUB 2.12 ext4 readerは先頭がholeのextentファイルで `something wrong with extent` を返す。ISO全体をハッシュする場合に発生するため、DATA生成時にextent/64bitを無効化して通常のブロック配置を使用。

## 自動検証の範囲

`make test` は停止したVMのディスクをホストで検査します。キャッシュ変更テストでは、DATA上のISOのパス・サイズ・mtimeを維持して中身だけを変更し、再展開された内容を確認します。失敗テストではFATの大小文字collisionを含むISOを選び、成功cacheが残らないことを確認します。USB接続ではQEMUのxHCIとUSB Mass Storageを使用します。

生成されたログは `.build/logs/`、イメージは `.build/validation/` に保存します。UDF・実機テストは未実行です。

最終統合テストではMilestone 1〜6・8〜10に加え、USB Mass StorageでのEFI引き継ぎと、destination traversalの拒否、FAT case collisionの拒否を確認しました。`scripts/deps.sh` の再実行と対話型 `scripts/run.sh` のシリアル起動も成功しました。

最終ソースでの統合テスト再実行も終了コード0で成功しました。既存の生成物がある状態での再実行を確認済みです。ディレクトリ清掃のbufferはheapに確保し、深い階層でも大きなstack bufferを積み重ねない構成にしています。

## 公式Linux ISOの検証

アクセス許可変更後、公式配布先からISOを取得できました。Alpine 3.22.3 standard x86_64のSHA-256は `4e05fdcf5d0cc8e7bd404d4512884bcce5f40f046f4adecbc84b06b83477cd1d` で、公式checksumと照合しました。

`make test-linux` はQEMU/OVMFのVGA画面をOCRで観測し、QMPで標準rootログインを行ってguestから証拠を取得します。kernelは `6.12.67-0-lts`、元の引数は `BOOT_IMAGE=/boot/vmlinuz-lts modules=loop,squashfs,sd-mod,usb-storage quiet`、boot mediaは `/dev/sda3` のFAT32で、通常接続時は `/media/sda3`、USB接続時は `/media/usb` に読み取り専用でmountされます。元のISOの120ファイルと8ディレクトリを展開し、全ファイルのバイト一致を検証します。ISO内のEFI・kernel・initramfs・GRUB設定に変更はありません。

Volume ID `alpine-std 3.22.3 x86_64` はFATの制限により反映できず、stock EFIのラベル検索は警告を出します。引き継いだscratchのrootからstock設定を読み込めるため、当該ISOではLinux起動を妨げません。標準initramfsがFATからboot mediaを発見し、標準のmodloopを読み込みます。

USBの標準mount先が `/media/usb` になることを実測し、自動テストのmount先判定を接続方式に合わせて修正しました。Linux側の変更は不要でした。

最終結果：通常接続の新規展開とUSB接続のcache再利用がともにLinux user spaceまで成功しました。USBの判定修正後はUSB段階を再実行して終了コード0を確認し、統合assertionと全ファイルのホスト比較を再実行して成功しました。通常接続の成功ログは保持しています。

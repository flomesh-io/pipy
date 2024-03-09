![Pipy Logo](./gui/src/images/pipy-300.png)

[English](./README.md) | [中文](./README_zh.md)

# Pipy

Pipyは、クラウド、エッジ、IoT向けのプログラム可能なネットワークプロキシです。C++言語で書かれたPipyは、究極的に軽量で高速です。また、Javaスクリプト言語をカスタマイズしたバージョンのPipyJSを使ってプログラミングが可能です。

## Pipyの特長

### 多目的

Pipyは高性能なリバースプロキシとして最も使われますが、本来の力は、さまざまなプラグインできる組立てブロック、すなわちフィルターであり、いかなる制約にも強いられずに、フィルターを組み合わせられることです。Pipyをどう使うのかは完全に皆さん次第ということです。これまで、プロトコルコンバータ、トラフィックレコーダ、メッセージsinger/verifier、サーバーレスファンクションスタータ、ヘルスチェッカー等々としてPipyが利用されたことがあります。

### 高速

PipyはC++で書かれています。非同期ネットワークを活用します。割り当てられたリソースはプールされて再利用されます。データは可能な限りポインタによって内部的に転送され、メモリ帯域幅への負担を最小限に抑えます。あらゆる点で高速にできています。

### 極小

ワーカーモードでPipyをビルドすると、その実行ファイルは10MB程度で、外部への依存性はゼロです。Pipyを使用すると、ダウンロードと起動の時間が最も速いのが実感できるでしょう。

### プログラミング性

Pipyの核となるのは、ECMA標準のJavaScriptをカスタマイズしたPipyJSを実行するスクリプトエンジンです。世界で最も普及しているプログラミング言語を使うことで、PipyはYAMLなどに比べても圧倒的な表現力を発揮します。

###オープン性

Pipyはオープンソースよりもオープンです。どんな情報も詳細をブラックボックスに隠したりはしないので、自分が何をしているかを常に把握できます。しかし、恐れることはありません。さまざまな部品がどのように連携するかを理解するために、ロケット科学者が必要なわけではないのです。実際、自分ですべてを完全にコントロールできるようになるので、もっと楽しくなるはずです。

## クイックスタート

### ビルド

ビルドするにあたっては、次の要件が必要です。

* Clang 5.0+
* CMake 3.10+
* Node.js v12+ (if the builtin _Admin UI_ is enabled)

ビルドを開始するには、次のビルドスクリプトを実行します。

```
./build.sh
```

最終的にできたものは`bin/pipy`に置かれます。

### 実行

`bin/pipy`をコマンドラインオプションなしで実行すると、Pipyがデフォルトのポート6060をリッスンし、repoモードで起動します。

```
$ bin/pipy

[INF] [admin] Starting admin service...
[INF] [listener] Listening on port 6060 at ::
```

お使いのブラウザを開いて、`http://localhost:6060`を指定します。すると_Admin UI_が起動し、ドキュメントの検索とチュートリアルコードベースの操作ができます。

## ドキュメンテーション

* [Overview](https://flomesh.io/pipy/docs/intro/overview)
* [Getting started](https://flomesh.io/pipy/docs/getting-started/build-install)
* [Tutorials](https://flomesh.io/pipy/docs/tutorial/01-hello)
* Reference
  * [API Reference](https://flomesh.io/pipy/docs/reference/api)
  * [PipyJS Reference](https://flomesh.io/pipy/docs/reference/pjs)
* Learning Resources
  * [Blog](https://blog.flomesh.io/)
  * [Katacoda](https://katacoda.com/flomesh-io) - Katacoda scenarios
  * [InfoQ article](https://www.infoq.com/articles/network-proxy-stream-processor-pipy/) - Brief Introduction
* [Copyright](COPYRIGHT)
* [License](LICENSE)

## 互換性

Pipyは常に以下のプラットフォームでテストされています。

* RHEL/CentOS
* Fedora
* Ubuntu
* Debian
* macOS
* FreeBSD
* OpenBSD
* OpenEuler
* OpenWrt
* Deepin
* Kylin

Pipyは以下のアーキテクチャで稼働できます。

* X86/64
* ARM64
* LoongArch
* Hygon

## 著作権およびライセンス

Please refer to [COPYRIGHT](https://github.com/flomesh-io/pipy/blob/main/COPYRIGHT) and [LICENSE](https://github.com/flomesh-io/pipy/blob/main/LICENSE).

## 問い合わせ

* セキュリティの問題についてはsecurity@flomesh.ioへ連絡ください。
* リーガルの問題についてはlegal@flomesh.ioへ連絡ください。
* 商用、販売、マーケティング関連は、sales@flomesh.ioへ連絡ください。
* その他で公開に適さないものはpipy@flomesh.ioへご連絡ください。
* 公開されているディスカッションについては、GitHubのhttps://github.com/flomesh-io/pipy/issues をご覧ください。

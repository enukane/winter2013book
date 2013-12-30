= 9な802.11がこの先生きのこるには
//lead{
この章では, Plan9/9frontでの無線LAN機能の実装状況についてざっと解説します.
//}
== 前置き: pre-9front時代の無線LAN
=== ソースを漁ってはみたものの....

そもそものモチベーション, 発端として最近無線LANに嵌まってる@<fn>{bad}こともあり 
「Plan 9での実装はあるのだろうか?」とふと疑問が沸いたことから今回のお話がはじまります.
//footnote[bad][いい意味でも悪い意味でも. シャドーボクシングたのしいれす.]

こういう場合, まずは何も考えずに"Wi-Fi"だとか"wireless LAN"だとか"802.11"だとか
それっぽい単語で, カーネルのソースが入っている/sys/src/9/あたりをgrepするところ
からはじめます.

するとでてくるではありませんか, wavelan.cとかwavelan.hとかそれらしき名前のファイルが！
いくらPCユースからはほぼうち捨てられているPlan 9とはいえ,
無線LANという今のご時世には無くてはならない機能もサポートできていたわけです.

サポートしているとおぼしきデバイスがPCMCIAで, 若造には聞き覚えのないもの
ばかりなのが気になるところですが, スタックさえあるならドライバなんて些細な問題です.

しかし, よくよくソースの中身を眺めていると
なにかがおかしいことに気付きます. 802.11スタックにあるべきBeaconだとかAssociation
だとかそういった単語が一切出てこないのです...

（＾ω＾ ≡ ＾ω＾）???

そもそも, Wi-Fiとか80211とかWirelessLANという名前で無くて@<b>{WaveLAN}という,
それっぽいけどよくよく思い起こせば聞いたことのない語を使っている
という点も気にかかってきます. もしかして, という悪い予感に目を背けながら
Google先生に聞いてみると我らがWikipedia先生の記事が引っかかりました.

//quote{
WaveLAN - http://en.wikipedia.org/wiki/WaveLAN

"Being a proprietary pre-802.11 protocol, it is completely incompatible with the 802.11 standard."
//}

ProximのOrinocoという名前を冠する無線LANデバイスは今でも販売中ですが,
その名前はここから来ていたのかーという合点体験もつかの間,
つまるところ"無線LAN"という機能に期待される現代的な802.11サポートというのは,
Plan 9には, ないのでした....@<fn>{wavelancomp}
//footnote[wavelancomp][念のため捕捉をしておくと, WaveLANのうちORiNOCOやWaveLAN IEEEと呼ばれるものたちは802.11と互換性があります]

=== plan9front
失意の中, やはり頼みになるのはplan9front@<fn>{9front_url}です.
Plan 9 from Bell Labs からforkして独自路線を突っ走りはじめた9frontですが,
見ない間にwifi.[ch]という名前のファイルとともに無線LANのサポートが増えていました.
最初のコミットが2013/02/08ということもあり, だいぶ若いコードですが
正しく802.11のスタック(の一部)を実装しています.
//footnote[9front_url][https://code.google.com/p/plan9front/]
//footnote[9front_wific][https://code.google.com/p/plan9front/source/browse/sys/src/9/pc/wifi.c]

wifi.cとか大層な名前をつけてしまって, Wi-Fi Alliance的にどうなのとかアーキテクチャ非依存
なんだから/sys/src/9/pcじゃなくて/sys/src/9/ipの部分においてほしいなぁとか,
メディアなんだからethermedium.cに倣ってwifimedium.cとかに改名してほしいなぁとか
いろいろと思うところはありますが, 何はともあれ実装が正義です.

前置きが長くなりましたがこの章では, このwifi.cとそこでサポートされている機能について
見ていきます.

== inside 9front 802.11
=== コールグラフ
wifi.cの主要な関数のコールグラフを作ってみると@<img>{wifi9_call}にあるような
図になります. まだまだ成長途中ということもあり, 一画面に収まるという非情な
までのシンプルさです. wifi.cとwifi.hを合わせても1400行以下しかありません.
それでもccmpとかtkipとかencrypt, decryptという文字列が見えたりとで, 暗号化・復号化もできそうな感じです.
ここからは大きく三つのパート, 通信管理の全体構造, 切断接続関連, 暗号化関連と分けて中身をさらっと見ていきます.
また, 合わせて無線LANアダプタのドライバの対応状況についても見ていきます.


//image[wifi9_call][コールグラフ]

=== 通信関連

ここではざっくりと, 通信全体の制御をどうしているかという部分に着目します.
802.11では, 一般的なデータ(Dataフレーム)の送受信に加えて
接続切断等のために使われるManagementフレームについても802.11スタックで
サポートする必要があります. この2つのフレームについてそれぞれ扱いを見ていきます.

共通部分の構造として, 802.11スタックの中心には3つのカーネルプロセスが存在します.
Plan 9のネットワークスタックでは, "kernel process"@<fn>{kproc}なるものを複数たてそれぞれ特有の
処理をさせるという形での実装が一般的です. いわゆるkernel threadっぽいものが
動いているイメージでしょうか. 
それぞれのkernel process (kproc)間や別の関数とのインタフェースにはQueueが用いられます.
このQueueは単純にデータ(フレームやパケット)をpush/popできるのに加え,
そのQueueに対する入力をkprocで待ち受ける, 待ち受けているkprocをたたき起こすといった
オペレーションが可能です@<fn>{qio}.
//footnote[kproc][http://9atom.org/magic/man2html/9/kproc]
//footnote[qio][http://9atom.org/magic/man2html/9/qio]

今回の802.11スタックでは無線LANインタフェースごとに以下のカーネルプロセスが動作しています(コールグラフ中の丸囲み).

 * wifiproc
 * wifoproc
 * wifsproc

wifiprocは受信処理用のプロセスです.ドライバ側からフレームが突っ込まれる受信キュー(wifi->iq)の監視, 
およびそこからの取り出しを行い, データなら復号化をしたり接続切断等のフレームなら後述する関数に割り振ったり
といったことをおこないます(後述しますが, データの受信に関しては別扱いになる場合があります).

wifoprocは送信処理用のプロセスです.これはwifiprocとは逆にEthernet層からの送信キュー(ether->oq)の監視をし, 
そこから取り出したethernetフレームを802.11フレームに変えたり, ペイロード部分を暗号化したりした後に, 
ドライバに引き渡すところまでの処理を行います.

wifsprocは無線LANクライアントとして, 無線LAN APのスキャンや接続状態の管理を
行うプロセスです. 定期的な周囲のAPのスキャンを行いつつ, 接続が完了した場合は
wifiproc側で受信したManagementフレームによる接続状態の変化を監視しつつ
場合に依っては再接続やスキャンのやり直しなどを行います.

==== Dataフレーム送受信フロー
===== 送信フロー
plan9frontでは, その他OSでもそうであるように802.11スタックは構造上Ethernetスタックの下位に
据えられています. 無線LANアダプタ/インタフェースは通常, OSに対してEthernetデバイスとして
登録されます. このため, 上位レイヤから見ればEthernetフレームをそのまま送信できるかのように振る舞います.

実際には, そのインタフェースでは802.11フレームのみしか受け付けられないため,
入力をフックしてEthernetフレームを802.11フレームに変換するといったことが内部的に
行われます. 9では, Ethernetスタックとインタフェース登録の間隙を縫ってこれを実現しています(@<img>{wifi9_tx}).

9でのEthernetスタックの最下部, インタフェースに対して実際にEthernetフレームを流し込む箇所では
以下の様なロジックでこれを行っています.

 1. そのインタフェースのoq (送信キュー)にフレームをpush
 2. インタフェース登録時にそのインタフェース用のtransmitハンドラが登録されていればそれを呼ぶ

通常は, transmitハンドラが実装されているため直接インタフェースの送信処理が実行されます.
802.11を扱う無線LANインタフェースではあえてここでtransmitハンドラを未実装にします.
その代わりをwifoprocプロセスが代行します.
wifoprocプロセスは, 前節でも述べたように送信キューの監視を行っています.
こちら側で入力を拾ってやることで, インタフェースへの送信をフックします.

wifoprocは, oqキューより得たフレームをwifietheroq関数に引き渡します.
この関数では先ほど述べたEthernet→802.11フレームへの変換を行い, wifitx関数にこれを引き渡します.
wifitxは先ほどフックされたインタフェースへのフレーム引き渡しを実際に行う関数です.
この流れにより, 最終的にインタフェースに処理済みのフレームが渡されます.
なお, wifitxでは暗号化が必要と判断した場合, wifiencrypt関数を呼ぶことで
TKIPないしCCMPによる暗号化をペイロード部分に対して行います.
//image[wifi9_tx][Tx側の処理の流れ(Ethernetとの対比)]

===== データ受信フロー

送信時同様, 受信時にもフックが必要です.
無線LANインタフェースが受信するフレームは802.11フレームであるため, Rx側では
802.11→Ethernetへの変換が必要になります.
Ethernetインタフェースデバイスでは, 通常受信割り込み内でetheriq関数を呼ぶことで
Ethernetスタックへフレームを引き渡します.
9な802.11スタックでは代わりにwifiiq関数を用意し, これを利用することとしています.

wifiiq関数は, 802.11フレーム全般の受信を扱う関数です.
Dataフレームを受信した場合は, この関数内で802.11ヘッダをEthernetヘッダにすげ替え,
etheriq関数を呼ぶことで上位レイヤにEthernetフレームを引き渡します.
ただし, 当該フレームが暗号化されていた場合復号化処理が必要になります.
この復号化についてはwifiiqでは行っておらず, 802.11用の受信キュー(wifi->iq)にpushし
wifiprocにいったん投げます.
実装次第ですがwifiiqは, 各ドライバからハードウェア割り込みで呼び出される可能性があります.
さすがに復号化処理までこのコンテキストで行うと長すぎるため, wifiprocに投げているものと
思われます.
wifiprocでは, 復号化が必要かどうかをFrame ControlのProtectedビットにて判断し,
その無線LAN 機器と折衝した鍵を用いて復号化を行います. 復号化が完了したら,
この時点で暗号化されていないフレームになっているので, wifiiqに再度渡すことで
Ethernetレイヤ(etheriq)まで一直線に上げられるようになります.
Ethernetレイヤまで一直線に上げます.

//image[wifi9_rx][Rx側の処理の流れ(Ethernetとの対比)]

==== Managementフレーム送受信フロー

前項のデータ受信フローでも述べた様に, Managementフレームについてもwifiiq関数が
そのエントリポイントになります.
ただし, Dataフレームは上位レイヤに上げる必要がありますが
Managementフレームは802.11 onlyの物であるため処理が異なります.
wifiiq内では特になにもされず, wifi->iqなる802.11フレーム処理用のキューにpush
されるのみになります. 実処理はwifiprocプロセスにて行われます.
一部の処理はwifiprocにハードコーディングされていますが, 基本的には次節に上げる
各Managementフレームの処理に紐付く関数を呼び出します.

なお, Data/Managementフレームの他にControlフレームという物が802.11にはありますが,
wifiiqではこれを華麗に無視するようになっています.
ControlフレームにはACKや, RTS/CTSやBlock ACKなどが含まれるためそこそこに
大事なのですが, 多くの場合ハードウェアで処理してくれるため
システム側の802.11スタック側に実装がなくてもあまり問題になりません.


=== 接続切断関連の実装状況

前述のManagementフレームの受信や, ユーザの要求に基づく送信等を適切に行うことで
"無線LANに繋ぐ"と言ったことが実現できます. Ethernetのように振る舞うためには
この物理的に殴ってもどうにもならない電波をうまくMAC層で制御して仮想的なリンクが
"つながった", "切れた"という挙動をサポートしてやる必要があります.

802.11ではManagementフレームの処理を正しく実装することでこれが実現できます.
一般的に, 無線LANクライアントと無線LANアクセスポイントの間では, 実際にデータの送受信をするに
あたって@<img>{wifi9_conn}にある様なManagementフレームの送受信が行われます.
この図には, 合わせて各々のManagementフレームを処理する, 802.11スタックでの関数名をマップして
あります.

//image[wifi9_conn][接続処理]

全体的にクライアント側には, 主要なハンドラが実装されている一方で
アクセスポイントとして適切なフレームを送信およびその状態を管理する機能が
未実装となっています.
また, クライアント側においてもこの接続管理は, だいぶ簡単な物となっています.
この部分はwifsprocプロセスが担っています.

=== 暗号化関連
コールグラフにもあるように, 暗号化／復号化のロジックが実装されています.
サポートしている暗号化スイートはCCMP(AES)とTKIPになります.
WEPについて一切言及していないあたりはなかなか男気溢れますね.

ただし, 後述するようにCCMPやTKIPを使うWPAではEAP over LAN (EAPOL)を使った
4-way Handshakeという手順が必要です. UNIX系ではhostapd/wpa_supplicantが
一般的にこの手順をやってくれますが, 9frontではPlan 9の認証系を扱う
factotumと呼ばれるシステムとうまく連携できるようにaux/wpa@<fn>{wpa}
(/sys/src/cmd/aux/wpa.c)コマンドを実装しています. またこれに合わせてfactotum
にも拡張が入っています(/sys/src/cmd/auth/factotum/wpapsk.c)
これにより, 以下の様なコマンドでSSIDとそれに紐付くパスフレーズを登録しておく
ことができます. この後は, 接続時にaux/wpaがよしなに4-Way Handshakeを走らせてくれます.

//emlist{
% aux/wpa -s (ここにSSID) -p (インタフェースのディレクトリ: /net/ether1)
!Adding key: proto=wpapsk essid=(ここに指定したSSID)
password: (パスフレーズを入力)
!
//}

//footnote[wpa][http://man.aiju.de/8/wpa]

=== 無線LANドライバ事情
802.11スタックの実装もそこそこ大変ですが, ドライバの方はどうでしょう, 主に数的な意味で.
現状, 少なくとも以下のPCIデバイスはサポートに含めようとしているようです.
なお, コードの存在のみの確認につき実際に動作するかは不明です.

 * ether2860.c : Ralink (VendorID: 0x1814)
 ** RT2890 (DevID: 0x0681)
 ** RT2790 (DevID: 0x0781)
 ** RT3090 (DevID: 0x3090)
 ** AwtRT2890 (DevID: 0x1059, VendorIDでマッチングしてないが...)
 * etheriwl.c : Intel (VendorID: 0x8086)
 ** WiFi Link 1000 (0x0084)
 ** WiFi Link 4965 (0x4229, 0x4230)
 ** WiFi Link 5300 AGN (0x4236)
 ** Wifi Link 5100 AGN (0x4237)
 ** Centrino Advanced-N 6205 (0x0085)
 ** Centrino Ultimate-N 6300 (0x422b)
 ** Centrino Wireless-N 100 (0x08ae)

//footnote[ralink][http://wikidevi.com/wiki/Ralink]
//footnote[intel][http://wikidevi.com/wiki/Intel]

上位からはEthernetデバイスと同じ扱いなのでしようがないのでしょうけれど,
ビルドの都合上無線LANデバイスにもかかわらず"ether"とのプレフィクスがついている
のはものすごい違和感がありますね...

Centrino系がカバーできているため, Wikideviで見るとそこそこにいろんなマシンで
使えそうで良い感じに見受けられます. あとはAtherosや, USB無線LANアダプタ系で
数が出ているRalink系の他のデバイスサポートが入るとなお良しな感じでしょうか.

ドライバの実装という面では, 基本的にはEthernetデバイスの様に書いて
transmitやctl, statあたりをwifi向けのそれにすげ替え, wifiattach関数を
attach時に呼ぶように変更すれば完了なので比較的独自の困難さは少なさそうです.

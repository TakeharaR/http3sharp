#include <random>
#include <inttypes.h>

#include "QuicheWindowsSample.h"


// quiche に受け渡す関数群定義
static void quiche_debug_output(const char* line, void* argp)
{
    fprintf(stderr, "[quiche DEBUG]%s\n", line);
}

static int for_each_header(uint8_t* name, size_t name_len, uint8_t* value, size_t value_len, void* argp)
{
    fprintf(stderr, "got HTTP header: %.*s=%.*s\n", (int)name_len, name, (int)value_len, value);
    return 0;
}

// ここから QuicheWrapper 関数
QuicheWrapper::QuicheWrapper(const char* host, const char* port)
    : _sock(-1), _config(nullptr), _conn(nullptr), _host(host)
{
    WSADATA    wsaData;
    WSAStartup(MAKEWORD(2, 0), &wsaData);
    _sock = CreateUdpSocket(_host, port);
    if (-1 == _sock)
    {
        return;
    }

    // quiche のデバッグログ出力に関数を繋ぐ
    quiche_enable_debug_logging(quiche_debug_output, nullptr);

    // quiche 関連の準備
    _config = CreateQuicheConfig();
    if (nullptr == _config)
    {
        return;
    }
    _conn = CreateQuicheConnection(_host, _config);
}

QuicheWrapper::~QuicheWrapper()
{
    if (nullptr != _conn)
    {
        quiche_conn_free(_conn);
    }
    if (nullptr != _config)
    {
        quiche_config_free(_config);
    }
    closesocket(_sock);
    WSACleanup();
}

int QuicheWrapper::Update()
{
    Send(_conn, _sock);
    return Receive(_sock, _conn);
}

int QuicheWrapper::Execute()
{
    if ((-1 == _sock))
    {
        // 初期化に失敗している
        return -1;
    }

    // handshake
    while (1)
    {
        if (0 > Update())
        {
            fprintf(stderr, "failed to recive.");
        }

        if (quiche_conn_is_established(_conn))
        {
            // 接続が確立してもすべてのパケットの受信が終わっていないことがある
            // 本来であれば非同期で Receive をまわしておくべきだがサンプルなので次でまとめて受け取る
            break;
        }

        // 送受信を少し待つ
        Sleep(16);
    }

    // 確立したコネクションのバージョンを確認
    {
        const uint8_t* app_proto;
        size_t app_proto_len;
        quiche_conn_application_proto(_conn, &app_proto, &app_proto_len);
        fprintf(stderr, "connection established: %.*s\n", (int)app_proto_len, app_proto);
    }

    // HTTP 通信用のストリームを作成する
    _http3stream = CreateHttpStream(_conn, _host);
    if (nullptr == _http3stream)
    {
        return -1;
    }

    
    // 結果の受信
    while (1)
    {
        if (0 > Update())
        {
            fprintf(stderr, "failed to recive.");
        }

        // HTTP 関連のイベントが quiche からきているかチェック(&来ていたら処理を実施)
        auto result = PollHttpResponse(_conn, _http3stream);
        if (-1 == result)
        {
            return -1;
        }
        else if (0 == result)
        {
            break;
        }

        // 送受信を少し待つ
        Sleep(16);
    }

    return 0;
}

void QuicheWrapper::Send(quiche_conn* _conn, SOCKET sock)
{
    static uint8_t out[MAX_DATAGRAM_SIZE] = { 0 };
    ssize_t totalSend = 0;

    while (1)
    {
        // quiche によって送信データ生成する
        // quiche_conn_send を呼ぶだけで、内部のステータス(コネクションやストリームの状況)に応じて適切なデータを生成してくれる
        ssize_t written = quiche_conn_send(_conn, out, sizeof(out));
        if (written == QUICHE_ERR_DONE)
        {
            break;
        }
        if (written < 0)
        {
            fprintf(stderr, "failed to create packet: %zd\n", written);
            return;
        }

        // quiche が生成したデータを UDP ソケットで送信する
        ssize_t sent = send(sock, (const char*)out, written, 0);
        if (sent != written)
        {
            perror("failed to send");
            return;
        }

        totalSend += sent;
    }

    if (0 < totalSend)
    {
        fprintf(stderr, "sent %zd bytes\n", totalSend);
    }
}

ssize_t QuicheWrapper::Receive(SOCKET sock, quiche_conn* conn)
{
    char buf[MAX_DATAGRAM_SIZE] = { 0 };
    ssize_t totalRead = 0;

    // UDP パケットを受信
    while (1)
    {
        ssize_t read = recv(sock, buf, sizeof(buf), 0);
        if (read < 0)
        {
            auto err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK)
            {
                break;
            }
            perror("failed to read");
            return -1;
        }
        totalRead += read;

        // 受信したパケットを quiche に渡す
        ssize_t done = quiche_conn_recv(conn, reinterpret_cast<uint8_t*>(buf), read);
        if (done < 0)
        {
            fprintf(stderr, "failed to process packet: %zd\n", done);
            return -1;
        }
    }
    if (0 < totalRead)
    {
        fprintf(stderr, "recv %zd bytes\n", totalRead);
    }

    if (quiche_conn_is_closed(conn))
    {
        fprintf(stderr, "Unintended connection closed\n");
        return -1;
    }

    return 0;
}

quiche_h3_conn* QuicheWrapper::CreateHttpStream(quiche_conn* conn, const char* host)
{
    // HTTP/3 用のコンフィグを作成する
    quiche_h3_config* config = quiche_h3_config_new();
    if (config == nullptr)
    {
        fprintf(stderr, "failed to create HTTP/3 config\n");
        return nullptr;
    }
    // HTTP/3 固有の設定
    quiche_h3_config_set_max_header_list_size(config, 1024);             // SETTINGS_MAX_HEADER_LIST_SIZE の設定。ヘッダリストに登録できるヘッダの最大数
    quiche_h3_config_set_qpack_max_table_capacity(config, 0);            // SETTINGS_QPACK_MAX_TABLE_CAPACITY の設定。QPACK の動的テーブルの最大値
    quiche_h3_config_set_qpack_blocked_streams(config, 0);               // SETTINGS_QPACK_BLOCKED_STREAMS の設定。ブロックされる可能性のあるストリーム数

    // HTTP/3 通信用のストリームハンドルを作成(このタイミングではまだ通信しない)
    auto http3stream = quiche_h3_conn_new_with_transport(conn, config);
    quiche_h3_config_free(config);
    if (http3stream == nullptr)
    {
        fprintf(stderr, "failed to create HTTP/3 connection\n");
        return nullptr;
    }

    // HTTP リクエストの作成
    quiche_h3_header headers[] =
    {
        {
            .name = (const uint8_t*) ":method",
            .name_len = sizeof(":method") - 1,

            .value = (const uint8_t*) "GET",
            .value_len = sizeof("GET") - 1,
        },

        {
            .name = (const uint8_t*) ":scheme",
            .name_len = sizeof(":scheme") - 1,

            .value = (const uint8_t*) "https",
            .value_len = sizeof("https") - 1,
        },

        {
            .name = (const uint8_t*) ":authority",
            .name_len = sizeof(":authority") - 1,

            .value = (const uint8_t*)host,
            .value_len = strlen(host),
        },

        {
            .name = (const uint8_t*) ":path",
            .name_len = sizeof(":path") - 1,

            .value = (const uint8_t*) "/",
            .value_len = sizeof("/") - 1,
        },

        {
            .name = (const uint8_t*) "user-agent",
            .name_len = sizeof("user-agent") - 1,

            .value = (const uint8_t*) "quiche",
            .value_len = sizeof("quiche") - 1,
        },
    };

    // quiche にヘッダリストを登録する(このタイミングではまだ通信は実施されない)
    auto streamId = quiche_h3_send_request(http3stream, conn, headers, 5, true);
    fprintf(stderr, "sent HTTP request %" PRId64 "\n", streamId);

    return http3stream;
}

// HTTP のレスポンス待ちをする関数
// -1 : エラー終了、 0 : 正常終了、 1 : 継続
int QuicheWrapper::PollHttpResponse(quiche_conn* conn, quiche_h3_conn* http3stream)
{
    quiche_h3_event* ev;
    char buf[MAX_DATAGRAM_SIZE] = { 0 };

    // quiche 側に HTTP のイベントが来ているかチェック
    // イベントを取り切るので while で回す
    while (1)
    {
        // ヘッダ受信、ボディ受信、ストリームのクローズの 3 種のイベントがある
        int64_t s = quiche_h3_conn_poll(http3stream, conn, &ev);
        if (s < 0)
        {
            break;
        }
        auto ev_type = quiche_h3_event_type(ev);

        switch (ev_type)
        {
            case QUICHE_H3_EVENT_HEADERS:
            {
                // HTTP ヘッダの受信完了
                // quiche_h3_event_for_each_header にコールバック関数を渡してヘッダを受け取る
                if (quiche_h3_event_for_each_header(ev, for_each_header, nullptr) != 0)
                {
                    perror("failed to process headers");
                    // ヘッダ不正でもクローズしたいので継続
                }
                // body の有無をチェックしたい場合は quiche_h3_event_headers_has_body で確認できる
                if (!quiche_h3_event_headers_has_body(ev))
                {
                    printf("no body");
                    // クローズ処理あるので継続
                }
                break;
            }

            case QUICHE_H3_EVENT_DATA:
            {
                // HTTP ボディの受信完了
                // ヘッダとは違いこちらはバッファを受け渡す形式
                // buf のサイズより受信データが小さい場合は再度呼び出すと途中からコピーしてくれる → 取りきれるまでループで回す必要がある
                while (1)
                {
                    ssize_t len = quiche_h3_recv_body(http3stream, conn, s, reinterpret_cast<uint8_t*>(buf), sizeof(buf));
                    if (len > 0)
                    {
                        _body.append(buf);
                    }
                    else
                    {
                        break;
                    }
                }
                break;
            }

            case QUICHE_H3_EVENT_FINISHED:
            {
                // ダウンロード終了
                printf("download finishe. HTTP body:\n %.*s", (int)_body.size(), _body.c_str());

                // ストリームがクローズされたので後始末
                quiche_h3_conn_free(http3stream);
                quiche_h3_event_free(ev);
                if (quiche_conn_close(conn, true, 0, nullptr, 0) < 0)
                {
                    perror("failed to close connection\n");
                    return -1;
                }
                else
                {
                    return 0;
                }
            }
        }
        quiche_h3_event_free(ev);
    }

    return 1;
}

// 非同期 UDP ソケットの生成関数
SOCKET QuicheWrapper::CreateUdpSocket(const char* host, const char* port)
{
    const struct addrinfo hints =
    {
        .ai_family = PF_UNSPEC,
        .ai_socktype = SOCK_DGRAM,
        .ai_protocol = IPPROTO_UDP
    };

    struct addrinfo* peer;
    int err = getaddrinfo(host, port, &hints, &peer);
    if (err != 0) {
        perror("failed to resolve host");
        return -1;
    }

    SOCKET sock = socket(peer->ai_family, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("failed to create socket");
        freeaddrinfo(peer);
        return -1;
    }

    u_long val = 1;
    ioctlsocket(sock, FIONBIO, &val);

    if (connect(sock, peer->ai_addr, peer->ai_addrlen) < 0) {
        perror("failed to connect socket");
        freeaddrinfo(peer);
        return -1;
    }

    freeaddrinfo(peer);
    return sock;
}

// quiche の config 設定を行う関数
quiche_config* QuicheWrapper::CreateQuicheConfig()
{
    // 引数には QUIC のバージョンを渡す
    // バージョンネゴシエーションを試したい時は 0xbabababa を渡すこと
    quiche_config* config = quiche_config_new(QUICHE_PROTOCOL_VERSION);
    if (config == nullptr)
    {
        fprintf(stderr, "failed to create config\n");
        return nullptr;
    }

    // quiche に HTTP/3 の ALPN token を設定する
    // quiche.h に定義されている QUICHE_H3_APPLICATION_PROTOCOL を渡せばいい
    quiche_config_set_application_protos(config, (uint8_t*)QUICHE_H3_APPLICATION_PROTOCOL, sizeof(QUICHE_H3_APPLICATION_PROTOCOL) - 1);

    // QUIC 固有の設定
    // 生成した config に対して設定を適用していく(下記の設定値は quiche の example に準拠)
    // 以下に無い quiche_config_set_max_ack_delay, quiche_config_set_ack_delay_exponent はクライアントからは呼ばないこと(サーバから降ってきた値を使用する)
    quiche_config_set_max_idle_timeout(config, 100);                             // max_idle_timeout の設定(ミリ秒)。 0 を指定する事でタイムアウトが無制限になる
    quiche_config_set_max_packet_size(config, MAX_DATAGRAM_SIZE);                // max_udp_payload_size の設定。 PMTU の実装を鑑みて設定を行うこと(「14. Packet Size」参照)
    quiche_config_set_initial_max_data(config, 10000000);                        // initial_max_data の設定(コネクションに対する上限サイズ)
    quiche_config_set_initial_max_stream_data_bidi_local(config, 1000000);       // initial_max_stream_data_bidi_local の設定(ローカル始動の双方向ストリームの初期フロー制御値)
    quiche_config_set_initial_max_stream_data_bidi_remote(config, 1000000);      // initial_max_stream_data_bidi_remote の設定(ピア始動の双方向ストリームの初期フロー制御値)
    quiche_config_set_initial_max_stream_data_uni(config, 1000000);              // initial_max_stream_data_uni の設定(単方向ストリームの初期フロー制御値)
    quiche_config_set_initial_max_streams_bidi(config, 100);                     // initial_max_streams_bidi の設定(作成可能な双方向ストリームの最大値)
    quiche_config_set_initial_max_streams_uni(config, 100);                      // initial_max_streams_uni の設定(作成可能な単方向ストリームの最大値)
    quiche_config_set_disable_active_migration(config, true);                    // disable_active_migration の設定(コネクションマイグレーションに対応していない場合は true にする)
    //quiche_config_verify_peer(config, false);                                    // 証明書の検証の on/off 。オレオレ証明書を使う際には false にする

    // TLS の鍵情報のダンプ。WireShark 等でパケットキャプチャする際に用いる
    // 一般的に環境変数 SSLKEYLOGFILE で制御する
    {
        size_t buf;
        char buffer[1024];
        if (getenv_s(&buf, buffer, 1024, "SSLKEYLOGFILE"))
        {
            quiche_config_log_keys(config);
        }
    }

    return config;
}

// quiche のコネクションを生成する関数
quiche_conn* QuicheWrapper::CreateQuicheConnection(const char* host, quiche_config* config)
{
    // Initial Packet で使用する SCID を乱数から生成する
    // SCID は QUIC バージョン 1 までは 20 バイト以内に抑える必要がある(今回は quiche の example の設定値に準拠)
    uint8_t scid[16] = {};
    std::random_device rd;
    std::mt19937_64 mt(rd());
    for (auto& id : scid)
    {
        id = mt() % 256;
    }

    // quiche のコネクションハンドルを作成する。この段階ではまだ通信は実施されない
    quiche_conn* conn = quiche_connect(host, (const uint8_t*)scid, sizeof(scid), config);
    if (conn == nullptr)
    {
        fprintf(stderr, "failed to create connection\n");
        return nullptr;
    }

    return conn;
}

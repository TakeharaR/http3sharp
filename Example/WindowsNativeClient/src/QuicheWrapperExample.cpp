#include <random>
#include <inttypes.h>

#include "QuicheWrapperExample.h"


// quiche �Ɏ󂯓n���֐��Q��`
static void quiche_debug_output(const char* line, void* argp)
{
	fprintf(stderr, "[quiche DEBUG]%s\n", line);
}

static int for_each_header(uint8_t* name, size_t name_len, uint8_t* value, size_t value_len, void* argp)
{
	fprintf(stderr, "got HTTP header: %.*s=%.*s\n", (int)name_len, name, (int)value_len, value);
	return 0;
}

// �������� QuicheWrapper �֐�
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

	// quiche �̃f�o�b�O���O�o�͂Ɋ֐����q��
	quiche_enable_debug_logging(quiche_debug_output, nullptr);

	// quiche �֘A�̏���
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

int QuicheWrapper::Execute()
{
	if ((-1 == _sock))
	{
		// �������Ɏ��s���Ă���
		return -1;
	}

	// handshake
	Send(_conn, _sock);
	while (1)
	{
		auto result = Receive(_sock, _conn);
		if (-1 == result)
		{
			return -1;
		}
		if (quiche_conn_is_established(_conn))
		{
			// �ڑ����m�����Ă����ׂẴp�P�b�g�̎�M���I����Ă��Ȃ����Ƃ�����
			// �{���ł���Δ񓯊��� Receive ���܂킵�Ă����ׂ������T���v���Ȃ̂Ŏ��ł܂Ƃ߂Ď󂯎��
			break;
		}
	}

	// �m�������R�l�N�V�����̃o�[�W�������m�F
	{
		const uint8_t* app_proto;
		size_t app_proto_len;
		quiche_conn_application_proto(_conn, &app_proto, &app_proto_len);
		fprintf(stderr, "connection established: %.*s\n", (int)app_proto_len, app_proto);
	}

	// HTTP �ʐM�p�̃X�g���[�����쐬����
	_http3stream = CreateHttpStream(_conn, _host);
	if (nullptr == _http3stream)
	{
		return -1;
	}

	// HTTP ���N�G�X�g�𑗕t
	Send(_conn, _sock);
	
	// ���ʂ̎�M
	while (1)
	{
		// HTTP �֘A�̃C�x���g�� quiche ���炫�Ă��邩�`�F�b�N(&���Ă����珈�������{)
		auto result = PollHttpResponse(_conn, _http3stream);
		if (-1 == result)
		{
			return -1;
		}
		else if (0 == result)
		{
			break;
		}
		auto recv_size = Receive(_sock, _conn);
		if (-1 == recv_size)
		{
			return -1;
		}
	}

	return 0;
}

void QuicheWrapper::Send(quiche_conn* _conn, SOCKET sock)
{
	static uint8_t out[MAX_DATAGRAM_SIZE] = { 0 };

	while (1) {
		ssize_t written = quiche_conn_send(_conn, out, sizeof(out));

		if (written == QUICHE_ERR_DONE) {
			fprintf(stderr, "done writing\n");
			break;
		}

		if (written < 0) {
			fprintf(stderr, "failed to create packet: %zd\n", written);
			return;
		}

		ssize_t sent = send(sock, (const char*)out, written, 0);
		if (sent != written) {
			perror("failed to send");
			return;
		}

		fprintf(stderr, "sent %zd bytes\n", sent);
	}
}

ssize_t QuicheWrapper::Receive(SOCKET sock, quiche_conn* conn)
{
	char buf[MAX_DATAGRAM_SIZE] = { 0 };

	// UDP �p�P�b�g����M
	ssize_t read = recv(sock, buf, sizeof(buf), 0);
	if (read < 0)
	{
		auto err = WSAGetLastError();
		if (err == WSAEWOULDBLOCK)
		{
			return 0;
		}
		perror("failed to read");
		return -1;
	}
	fprintf(stderr, "recv %zd bytes\n", read);

	// ��M�����p�P�b�g�� quiche �ɓn��
	ssize_t done = quiche_conn_recv(conn, reinterpret_cast<uint8_t*>(buf), read);
	if (done == QUICHE_ERR_DONE)
	{
		// Windows �łł͂����ɓ���Ȃ����Ƃ�����̂ŕʂ̃g���K�[�Ŋ���������K�v������
		fprintf(stderr, "done reading\n");
		return read;
	}
	else if (done < 0)
	{
		fprintf(stderr, "failed to process packet: %zd\n", done);
		return -1;
	}

	if (quiche_conn_is_closed(conn))
	{
		fprintf(stderr, "connection closed\n");
		return -1;
	}

	return read;
}

quiche_h3_conn* QuicheWrapper::CreateHttpStream(quiche_conn* conn, const char* host)
{
	// HTTP/3 �p�̃R���t�B�O���쐬����
	// quiche_h3_config_new �����͈ȉ�(�ݒ�l�� quiche �T���v���̂��̂����̂܂ܓK�p)
	// uint64_t num_placeholders : �v���C�I���e�B�Ɋւ���v���[�X�z���_�[�̐ݒ�(�v���C�I���e�B�폜�ςׂ݂̈ɕs�g�p)
	// uint64_t max_header_list_size : �w�b�_���X�g�ɓo�^�ł���w�b�_�̍ő吔
	// uint64_t qpack_max_table_capacity : QPACK �̓��I�e�[�u���̍ő�l
	// uint64_t qpack_blaocked_streams : �u���b�N�����\���̂���X�g���[����
	quiche_h3_config* config = quiche_h3_config_new(0, 1024, 0, 0);
	if (config == nullptr)
	{
		fprintf(stderr, "failed to create HTTP/3 config\n");
		return nullptr;
	}

	// HTTP/3 �ʐM�p�̃X�g���[���n���h�����쐬(���̃^�C�~���O�ł͂܂��ʐM���Ȃ�)
	auto http3stream = quiche_h3_conn_new_with_transport(conn, config);
	quiche_h3_config_free(config);
	if (http3stream == nullptr)
	{
		fprintf(stderr, "failed to create HTTP/3 connection\n");
		return nullptr;
	}

	// HTTP ���N�G�X�g�̍쐬
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
	// quiche �Ƀw�b�_���X�g��o�^����(���̃^�C�~���O�ł͂܂��ʐM�͎��{����Ȃ�)
	int64_t stream_id = quiche_h3_send_request(http3stream, conn, headers, 5, true);
	fprintf(stderr, "sent HTTP request %" PRId64 "\n", stream_id);

	return http3stream;
}

// HTTP �̃��X�|���X�҂�������֐�
// -1 : �G���[�I���A 0 : ����I���A 1 : �p��
int QuicheWrapper::PollHttpResponse(quiche_conn* conn, quiche_h3_conn* http3stream)
{
	quiche_h3_event* ev;
	char buf[MAX_DATAGRAM_SIZE] = { 0 };

	while (1)
	{
		int64_t s = quiche_h3_conn_poll(http3stream, conn, &ev);
		if (s < 0)
		{
			break;
		}
		auto ev_type = quiche_h3_event_type(ev);
		quiche_h3_event_free(ev);

		switch (ev_type)
		{
			case QUICHE_H3_EVENT_HEADERS:
			{
				if (quiche_h3_event_for_each_header(ev, for_each_header, nullptr) != 0)
				{
					perror("failed to process headers");
					// �w�b�_�s���ł��N���[�Y�������̂Ōp��
				}
				break;
			}

			case QUICHE_H3_EVENT_DATA:
			{
				ssize_t len = quiche_h3_recv_body(http3stream, conn, s, reinterpret_cast<uint8_t*>(buf), sizeof(buf));
				if (len > 0)
				{
					printf("got HTTP body: %.*s", (int)len, buf);
				}
				break;
			}

			case QUICHE_H3_EVENT_FINISHED:
			{
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
	}

	return 1;
}

// �񓯊� UDP �\�P�b�g�̐����֐�
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

// quiche �� config �ݒ���s���֐�
quiche_config* QuicheWrapper::CreateQuicheConfig()
{
	// �����ɂ� QUIC �̃o�[�W������n��
	// �o�[�W�����l�S�V�G�[�V������������������ 0xbabababa ��n������
	quiche_config* config = quiche_config_new(0xff000017);
	if (config == nullptr)
	{
		fprintf(stderr, "failed to create config\n");
		return nullptr;
	}

	// quiche �� HTTP/3 �� ALPN token ��ݒ肷��
	// quiche.h �ɒ�`����Ă��� QUICHE_H3_APPLICATION_PROTOCOL ��n���΂���
	quiche_config_set_application_protos(config, (uint8_t*)QUICHE_H3_APPLICATION_PROTOCOL, sizeof(QUICHE_H3_APPLICATION_PROTOCOL) - 1);

	// �������� config �ɑ΂��Đݒ��K�p���Ă���(���L�̐ݒ�l�� quiche �� example �ɏ���)
	// �ȉ��ɖ��� quiche_config_set_max_ack_delay, quiche_config_set_ack_delay_exponent �̓N���C�A���g����͌Ă΂Ȃ�����(�T�[�o����~���Ă����l���g�p����)
	quiche_config_set_idle_timeout(config, 100);
	quiche_config_set_max_packet_size(config, MAX_DATAGRAM_SIZE);				// UDP �p�P�b�g�ő�T�C�Y�B Google �̒����ɂ�� QUIC �ł� 1350 ������
	quiche_config_set_initial_max_data(config, 10000000);						// initial_max_data �̐ݒ�(�R�l�N�V�����ɑ΂������T�C�Y)
	quiche_config_set_initial_max_stream_data_bidi_local(config, 1000000);		// initial_max_stream_data_bidi_local �̐ݒ�(���[�J���n���̑o�����X�g���[���̏����t���[����l)
	quiche_config_set_initial_max_stream_data_bidi_remote(config, 1000000);		// initial_max_stream_data_bidi_remote �̐ݒ�(�s�A�n���̑o�����X�g���[���̏����t���[����l)
	quiche_config_set_initial_max_stream_data_uni(config, 1000000);				// initial_max_stream_data_uni �̐ݒ�(�P�����X�g���[���̏����t���[����l)
	quiche_config_set_initial_max_streams_bidi(config, 100);					// initial_max_streams_bidi �̐ݒ�(�쐬�\�ȑo�����X�g���[���̍ő�l)
	quiche_config_set_initial_max_streams_uni(config, 100);						// initial_max_streams_uni �̐ݒ�(�쐬�\�ȒP�����X�g���[���̍ő�l)
	quiche_config_set_disable_migration(config, true);							// disable_active_migration �̐ݒ�(�R�l�N�V�����}�C�O���[�V�����ɑΉ����Ă��Ȃ��ꍇ�� false �ɂ���)
	quiche_config_verify_peer(config, false);									// �ؖ����̌��؂� on/off �B�I���I���ؖ������g���ۂɂ� false �ɂ���

	// TLS �̌����̃_���v�BWireShark ���Ńp�P�b�g�L���v�`������ۂɗp����
	// ��ʓI�Ɋ��ϐ� SSLKEYLOGFILE �Ő��䂷��
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

// quiche �̃R�l�N�V�����𐶐�����֐�
quiche_conn* QuicheWrapper::CreateQuicheConnection(const char* host, quiche_config* config)
{
	// Initial Packet �Ŏg�p���� SCID �𗐐����琶������
	// SCID �� QUIC �o�[�W���� 1 �܂ł� 20 �o�C�g�ȓ��ɗ}����K�v������(����� quiche �� example �̐ݒ�l�ɏ���)
	uint8_t scid[16] = {};
	std::random_device rd;
	std::mt19937_64 mt(rd());
	for (auto& id : scid)
	{
		id = mt() % 256;
	}

	// quiche �̃R�l�N�V�����n���h�����쐬����B���̒i�K�ł͂܂��ʐM�͎��{����Ȃ�
	quiche_conn* conn = quiche_connect(host, (const uint8_t*)scid, sizeof(scid), config);
	if (conn == nullptr)
	{
		fprintf(stderr, "failed to create connection\n");
		return nullptr;
	}

	return conn;
}

#include <cassert>
#include <sstream>

#include "qwfs_h3_stream.h"
#include <iomanip>

const uint64_t DEFAULT_HEADERS_NUM = 5U;

namespace qwfs
{
    Stream::Stream(
        QwfsId externalId
        , const char* saveFilePath
        , const char* hostName
        , const char* path
        , QwfsRequestHeaders* userHeaders
        , uint64_t headersSize
        , const QwfsCallbacks& callbacks
        , DebugOutputCallback debugOutput
    ) :
        _status(QwfsStatus::Connecting)
        , _externalId(externalId)
        , _callbacks(callbacks)
        , _debugOutput(debugOutput)
        , _quicheQuicConnection(nullptr)
        , _quicheH3Connection(nullptr)
        , _streamId(0U)
        , _file(nullptr)
        , _body(nullptr)
        , _bodySize(0U)
        , _writeSize(0U)
        , _progress(0U)
        , _responseCode(0U)
        , _hostName(hostName)
    {
        if (nullptr != saveFilePath)
        {
            _saveFilePath = saveFilePath;
        }

        // �w�b�_���X�g�̍쐬
        if ((nullptr == path) || (0 == strlen(path)))
        {
            path = "/";
        }

        auto headerNum = headersSize + DEFAULT_HEADERS_NUM;
        _reqHeadersForQuiche = new quiche_h3_header[headerNum];
        _reqHeaders.reserve(headerNum);

        // Pseudo - Header ��ǉ�
        _reqHeaders.emplace_back(":method", "GET");
        _reqHeaders.emplace_back(":scheme", "https");
        _reqHeaders.emplace_back(":authority", _hostName);
        _reqHeaders.emplace_back(":path", path);
        _reqHeaders.emplace_back("user-agent", "quiche");

        // ���[�U��`�w�b�_��ǉ�
        if (nullptr != userHeaders)
        {
            for (auto num = 0; num < headersSize; ++num)
            {
                _reqHeaders.emplace_back(userHeaders[num].name, userHeaders[num].value);
            }
        }

        for (auto num = 0; num < _reqHeaders.size(); ++num)
        {
            _reqHeadersForQuiche[num].name = reinterpret_cast<const uint8_t*>(_reqHeaders[num]._name.c_str());
            _reqHeadersForQuiche[num].name_len = _reqHeaders[num]._name.size();
            _reqHeadersForQuiche[num].value = reinterpret_cast<const uint8_t*>(_reqHeaders[num]._value.c_str());
            _reqHeadersForQuiche[num].value_len = _reqHeaders[num]._value.size();
        }
    }

    Stream::~Stream()
    {
        Clear();
        if (nullptr != _reqHeadersForQuiche)
        {
            delete[] _reqHeadersForQuiche;
        }
    }

    int64_t Stream::Start(quiche_conn* quicheConnection, quiche_h3_conn* quicheH3Connection)
    {
        assert(nullptr != quicheConnection);
        assert(nullptr != quicheH3Connection);

        // �t�@�C���ۑ�����
        if (!_saveFilePath.empty())
        {
            // todo : ���{��t�@�C�����Ή� and �ꎞ�t�@�C���쐬�Ή�
            _file = fopen(_saveFilePath.c_str(), "wb");
            if (nullptr == _file)
            {
                std::stringstream ss;
                ss << "[qwfs error][" << _hostName << "][STREAM:" << _streamId << "]Failed to open file(" << _saveFilePath << ")" << std::endl;
                SetStatus(QwfsStatus::ErrorFileIO, ss.str().c_str());
                return -1;
            }
        }

        _quicheQuicConnection = quicheConnection;
        _quicheH3Connection = quicheH3Connection;
        _streamId = quiche_h3_send_request(_quicheH3Connection, _quicheQuicConnection, _reqHeadersForQuiche, _reqHeaders.size(), true);
        if (0 > _streamId)
        {
            // todo : ������ -12 ���Ԃ��Ă����ꍇ�� peer �������� local �� initial_max_streams_bidi ������̂��̏���������葽���̃X�g���[����ς����Ƃ������ɂȂ�
            // ���΂炭�܂��ă��g���C���鎖�ɂ������������Ƃ��낾���A quiche ���� local_opened_streams_bidi ���f�N�������g����Ȃ���肪�����x������ƕ����s�\�ɂȂ�ׂɌ���N���e�B�J���G���[�̂܂܂ɂ��Ă���
            std::stringstream ss;
            ss << "[qwfs error][" << _hostName << "][STREAM:" << _streamId << "]Failed to quiche_h3_send_request(" << _saveFilePath << ")" << std::endl;
            SetStatus(QwfsStatus::CriticalErrorQuiche, ss.str().c_str());
        }

        if (nullptr != _debugOutput)
        {
            std::stringstream ss;
            ss << "[qwfs info][" << _hostName << "][STREAM:" << _streamId << "]Start request" << std::endl;
            _debugOutput(ss.str().c_str());
        }

        return _streamId;
    }

    int64_t Stream::Retry()
    {
        Clear();
        SetStatus(QwfsStatus::Connecting);
        return Start(_quicheQuicConnection, _quicheH3Connection);
    }

    QwfsStatus Stream::EventHeaders(quiche_h3_event* event)
    {
        // HTTP �w�b�_�̎�M���I������̂ŏ�������

        // quiche_h3_event_for_each_header �͎��O�Ɍ����킩��Ȃ��̂łƂ肠���� 1 �ϐ��ɋl�߂ĕ����� C# �ł��
        // todo : ������
        auto getHeader = [](uint8_t* name, size_t name_len, uint8_t* value, size_t value_len, void* argp) -> int
        {
            auto stream = reinterpret_cast<Stream*>(argp);
            std::string headerName(reinterpret_cast<char*>(name), name_len);
            std::string headerValue(reinterpret_cast<char*>(value), value_len);

            if ("content-length" == headerName)
            {
                stream->_bodySize = std::stoull(headerValue);
            }

            if (":status" == headerName)
            {
                stream->_responseCode = std::stoull(headerValue);
            }

            stream->_resHeaders.push_back(std::move(headerName));
            stream->_resHeaders.push_back(std::move(headerValue));

            // todo : �s���w�b�_�m�F�������ꍇ�͂����Ń`�F�b�N���Ĕ� 0 ��Ԃ�(���f�����)

            return 0;
        };
        quiche_h3_event_for_each_header(event, getHeader, this);

        // Body ���Ȃ��ꍇ���p������ QUICHE_H3_EVENT_FINISHED �ցH(�v�m�F)

        return _status;
    }

    QwfsStatus Stream::EventData()
    {
        // HTTP �{�f�B�̎�M��ςݏグ��

        // quiche ����̎�M�����o�b�t�@���󂯎��B��x�Ŏ󂯎��Ȃ����Ƃ�����(��w�� quiche_h3_conn_poll �ł܂������ɂ���)
        char buf[MAX_DATAGRAM_SIZE] = { 0 };
        auto recvSize = quiche_h3_recv_body(_quicheH3Connection, _quicheQuicConnection, _streamId, reinterpret_cast<uint8_t*>(buf), sizeof(buf));
        if (0 > recvSize)
        {
            std::stringstream ss;
            ss << "[qwfs error][" << _hostName << "][STREAM:" << _streamId << "]Failed to quiche_h3_recv_body(quiche_error is " << recvSize << ")." << std::endl;
            return SetStatus(ConvertQuicheH3ErrorToStatus(static_cast<quiche_h3_error>(recvSize), _status), ss.str().c_str());
        }

        // �������ݏ���
        if (nullptr != _file)
        {
            fwrite(buf, recvSize, 1, _file);
        }
        else
        {
            if (0U == _writeSize)
            {
                if (0U != _bodySize)
                {
                    _body = reinterpret_cast<char*>(malloc(_bodySize));
                }
                else
                {
                    _body = reinterpret_cast<char*>(malloc(recvSize));
                }
            }
            else
            {
                if (0U == _bodySize)
                {
                    _body = reinterpret_cast<char*>(realloc(reinterpret_cast<void*>(_body), _writeSize + recvSize));
                }
            }
            std::memcpy(reinterpret_cast<void*>(&_body[_writeSize]), buf, recvSize);
        }
        _writeSize += recvSize;
        _progress += recvSize;

        return _status;
    }

    void Stream::EventFinished()
    {
        if (nullptr != _debugOutput)
        {
            std::stringstream ss;
            ss << "[qwfs info][" << _hostName << "][STREAM:" << _streamId << "]Complete request" << std::endl;
            _debugOutput(ss.str().c_str());
        }
        SetStatus(QwfsStatus::Completed);
    }

    void Stream::Shutdown(QwfsStatus status, const char* cause)
    {
        // �����I��
        if (QwfsStatus::Connecting == _status)
        {
            // �ʂ̎��s�R�[���o�b�N�͌Ă΂Ȃ�
            // ���g���C���ɕs�v�ȃf�[�^�͂��̎��_�ŉ�����Ă���
            Clear();
            SetStatus(status, cause);
        }
    }

    void Stream::AddProgress(uint64_t* progress, uint64_t* totalWriteSize)
    {
        *progress += _progress;
        *totalWriteSize += _writeSize;
        _progress = 0U;
    }

    void Stream::CallSuccessCallback(QwfsId hostId)
    {
        // todo : ������
        std::vector<const char*> headers;
        for (auto& header : _resHeaders)
        {
            headers.push_back(header.c_str());
        }

        if (nullptr != _file)
        {
            if (nullptr != _callbacks._successFile)
            {
                _callbacks._successFile(hostId, _responseCode, headers.data(), _resHeaders.size(), _saveFilePath.c_str());
            }
        }
        else
        {
            if (nullptr != _callbacks._successBinary)
            {
                _callbacks._successBinary(hostId, _responseCode, headers.data(), _resHeaders.size(), _body, _writeSize);
            }
        }
    }

    void Stream::CallErrorCallback(QwfsId hostId)
    {
        if (nullptr != _callbacks._error)
        {
            _callbacks._error(hostId, _status, _errorDetail.c_str());
        }
    }

    QwfsStatus Stream::SetStatus(QwfsStatus status, const char* errorDetail)
    {
        _status = status;
        if (nullptr != errorDetail)
        {
            _errorDetail = errorDetail;
            if (nullptr != _debugOutput)
            {
                _debugOutput(errorDetail);
            }
        }
        else
        {
            _errorDetail.clear();
        }
        return _status;
    }

    void Stream::Clear()
    {
        _streamId = 0U;

        _resHeaders.clear();

        if (nullptr != _file)
        {
            fclose(_file);
            _file = nullptr;
        }
        if (nullptr != _body)
        {
            free(_body);
            _body = nullptr;
        }
        _bodySize = 0U;
        _writeSize = 0U;
        _progress = 0U;
        _responseCode = 0U;
    }

    QwfsStatus Stream::ConvertQuicheH3ErrorToStatus(quiche_h3_error error, QwfsStatus nowStatus) const
    {
        // ConvertQuicheErrorToStatus �Ɠ���
        switch (error)
        {
        // �����I�ɒʐM���s�Œ��肻���Ȃ���
        case QUICHE_H3_ERR_DONE:
        case QUICHE_H3_ERR_TRANSPORT_ERROR:         // �����I�Ƀ��g���C���ď���ɒʐM��������悤�Ȃ̂ŉ������Ȃ�(����m�F�ς�)
        case QUICHE_H3_ERR_STREAM_BLOCKED:          // QUIC ���ɗ]�T���ł���΃��g���C�Œ�����ۂ��̂ł����ɂ��Ă���(���얢�m�F)
        case QUICHE_H3_ERR_EXCESSIVE_LOAD:          // �ߕ��ׂɂ���t�s���䔭�����ɋN������ۂ��̂ł����ɂ��Ă���(���얢�m�F)
        case QUICHE_H3_ERR_FRAME_UNEXPECTED:        // �T�[�o�����Ԉ�����t���[���𑗂��Ă����Ƃ����̕ӂ̋����H���ۂ��̂ł����ɂ��Ă���(���얢�m�F)
        case QUICHE_H3_ERR_FRAME_ERROR:             // (���얢�m�F)
            return nowStatus;

        // ���g���C�Œ��肻���Ȃ���
        case QUICHE_H3_ERR_CLOSED_CRITICAL_STREAM:  // (���얢�m�F)
            return QwfsStatus::Error;   // �K���ȃX�e�[�^�X��Ԃ��� Connection ���Ń��g���C���Ă��炤

            // ������̎����~�X�Ŕ����������
        case QUICHE_H3_ERR_BUFFER_TOO_SHORT:    // (���얢�m�F)
        case QUICHE_H3_ERR_ID_ERROR:            // �X�g���[�� ID ���g���܂킳�ꂽ��A����≺���𒴂���Ɣ���
            assert(false);
            return QwfsStatus::CriticalError;

            // �T�[�o����̃f�[�^�������������̓����G���[���ۂ����́B�T�[�o�̖��Ȃ̂Ōq�������Ă��_��
        case QUICHE_H3_ERR_STREAM_CREATION_ERROR:       // (���얢�m�F)
        case QUICHE_H3_ERR_MISSING_SETTINGS:            // (���얢�m�F)
        case QUICHE_H3_ERR_QPACK_DECOMPRESSION_FAILED:  // (���얢�m�F)
            return QwfsStatus::ErrorInvalidResponse;

        case QUICHE_H3_ERR_INTERNAL_ERROR:
        default:
            return QwfsStatus::CriticalErrorQuiche;
        }
    }
}
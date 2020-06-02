#include <chrono>
#include <thread>

#include "qwfs.h"

static void NormalDebugOutput(const char* line)
{
    fprintf(stderr, "%s\n", line);
}

static void QuicheDebugOutput(const char* line, void* arg)
{
    auto func = reinterpret_cast<DebugOutputCallback>(arg);
    func(line);
}

// ��Ő���
namespace QwfsTest
{
    void Excute(const char* host, const char* port, bool verifyPeer)
    {
        qwfsSetDebugOutput(NormalDebugOutput);

        QwfsCallbacks callbacks;

        callbacks._successBinary = [](QwfsId externalId, uint64_t responseCode, const char** headers, uint64_t headersSize, const void* body, uint64_t bodySize)
        {
            int hoge = 0;
        };

        callbacks._successFile = [](QwfsId externalId, uint64_t responseCode, const char** headers, uint64_t headersSize, const char* filePath)
        {
            int hoge = 0;
        };

        callbacks._error = [](QwfsId externalId, QwfsStatus status, const char* errorDetail)
        {
            int hoge = 0;
        };

        auto hostId = qwfsCreate(host, port, callbacks);

        QwfsOptions options;
        options._qlogPath = "F://tmp";
        options._verifyPeer = verifyPeer;
        qwfsSetOptions(hostId, options);

        // ��������
        uint64_t memoryId = 0;
        uint64_t fileId = 1;
        for (auto ii = 0; ii < 128; ++ii)
        {
            qwfsGetRequest(hostId, memoryId, "1024 * 1024", nullptr, nullptr, 0);
            
            // �t�@�C���ۑ���
            // qwfsGetRequest(hostId, fileId, nullptr, "barbara.txt", nullptr, 0);
        }

        auto status = QwfsStatus::Connecting;
        do 
        {
            qwfsUpdate(hostId);
            qwfsIssueCallbacks(hostId);

            //std::this_thread::sleep_for(std::chrono::milliseconds());

            qwfsGetStatus(hostId, &status);
        } while (QwfsStatus::Connecting == status);
        
        qwfsDestroy(hostId);
    }
};


// �T�[�o�̎w��
#if 0
// �O���T�C�g���g���ꍇ
const char* HOST = "quic.aiortc.org";
//const char* HOST = "blog.cloudflare.com";
const char* PORT = "443";
#else
const char* HOST = "localhost";
const char* PORT = "4433";
#endif

int main()
{
    qwfsInitialize();
    QwfsTest::Excute(HOST, PORT, false);
    //QwfsTest::Excute("blog.cloudflare.com", "443", true);
    qwfsUninitialize();

    // �ď������`�F�b�N
    //qwfsInitialize();
    //QwfsTest::Excute(HOST, PORT, false);
    //QwfsTest::Excute("blog.cloudflare.com", "443", true);
    //qwfsUninitialize();
}
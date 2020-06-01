http3sharp �� HTTP/3 �ʐM�p�� Unity �����A�Z�b�g�ł��B
[quiche](https://github.com/cloudflare/quiche) �� C/C++ ���b�p�w�ł��� qwfs �ƁA qwfs ��p���� C# ���� HTTP/3 �N���C�A���g���C�u�����ł��� http3sharp ���琬��܂��B


# http3sharp

http3sharp �� Unity �� HTTP/3 �ʐM���s������ړI�Ƃ��� Unity �A�Z�b�g�ł��B
``Http3sharp`` �f�B���N�g�����w�肵�� Unity �Ńv���W�F�N�g���J�����A���̂܂܃C���|�[�g���Ă��������B

���s�ɂ� qwfs �̃r���h���K�v�ł��B
��q����菇�ŕK�� qwfs �̃r���h���s���Ă��������B
qwfs �̃r���h����������� ``Http3sharp/Assets/Http3Sharp/Plugins\Windows`` �t�H���_�� quiche.dll �y�� qwfs.dll �������I�ɃR�s�[�����̂ŁA dll �̎蓮�R�s�[�͕s�v�ł��B


## ���m�̖��E��������

- �݌v���N�G�X�g�� peer �y�� client �� ``initial_max_streams_bidi`` �̒l�𒴂���ƃ��N�G�X�g�����s�ł��Ȃ��Ȃ�܂�
    - ����蔭�����ɂ� Http3Sharp �C���X�^���X�̍Đ����������� ``Http3Sharp.Abort`` �Ăяo�����s���Ă�������
- quiche �̃f�o�b�O���O�o�͂ɂ͔�Ή��ł�
- �t�@�C������ qlog �ۑ��̃p�X���ɓ��{��̃t�@�C���p�X���w�肷��ƃN���b�V�����܂�
- �œK���͈�؎{���Ă��܂���
- ���̑��\�[�X�R�[�h���� todo �ƃR�����g��������͏C���\��ł�



# qwfs

qwfs (quiche wrapper for sharp) �� [quiche](https://github.com/cloudflare/quiche) �� Unity �ɑg�ݍ��݈Ղ����b�s���O���� C/C++ ���̃��C�u�����ł��B
http3sharp �̗��p�ɓ��������d�l�Ŏ�������Ă���̂ŒP�̂ł̎g�p�͔񐄏��ł��B

#### �r���h���@

Visual Studio 2019 ���K�v�ł��B
``External/quiche`` �� quiche �� submodule �ݒ�ɂ��Ă���܂��̂ŁA������ quiche ���r���h���Ă��� ``Example/QuicheWindowsSample/QuicheWindowsSample.sln`` ���g���r���h���s���Ă��������B

quiche �� .lib/.dll �r���h���@�͈ȉ����Q�Ƃ��Ă��������B  
[Re: C#(Unity)��HTTP/3�ʐM���Ă݂� ���̈� �`OSS�̑I�肩��r���h�܂Ł`](https://qiita.com/takehara-ryo/items/1f3e19b54bd6fffcba77)


# QuicheWindowsSample

[quiche](https://github.com/cloudflare/quiche) �� example �� Windows �ڐA�������̂ł��B
���Ƃ��Ă��� quiche �� example �R�[�h�̃o�[�W������ 0.4.0 �ł��B  
�ڍׂ͈ȉ��̋L�����Q�Ƃ��Ă��������B
[Re: C#(Unity)��HTTP/3�ʐM���Ă݂� ���̓� �`Windows��quiche�̃T���v���𓮂����Ă݂�`](https://qiita.com/takehara-ryo/items/4cbb5c291b2e94b4eafd)
�����T���v���͍��チ���e�i���X����Ȃ��\��������܂�

�r���h�ɂ� http3sharp �Ɠ��l�� quiche �̃r���h���K�v�ł��B
``External/quiche`` �� quiche �� submodule �ݒ�ɂ��Ă���܂��̂ŁA������ quiche ���r���h���Ă��� ``qwfs/qwfs.sln`` ���g���r���h���s���Ă��������B


# ���ӎ���

���݂̍ŐV�o�[�W������ 0.1.0 �ł��B
0.1.0 �͎����I�����ł���A�C���^�[�t�F�[�X��d�l�����̂��̂ł��B
����A�j��I�ȕύX�𔺂��啝�ȏC��������鎖������܂��B
���p���ɂ͂����ӂ��������B


# �Ή��v���b�g�t�H�[��

�o�[�W���� 0.1.0 �ł� Windows �ɂ̂ݑΉ����Ă��܂��B
�����I�ɂ� Android/iOS �ɑΉ��\��ł��B


# �o�[�W�����֘A���

- �Ή����Ă��� HTTP/3, QUIC �� draft �o�[�W���� : 27
- ����m�F���Ă��� Unity �o�[�W���� : 2019.3.14f
- quiche : 0.4.0
- boringssl : quiche �� submodule �� �o�[�W���� �ɏ�����


# License
This software is released under the MIT License, see LICENSE.
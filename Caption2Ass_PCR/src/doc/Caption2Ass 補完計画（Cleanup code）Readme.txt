
Caption2Ass �⊮�v��iCleanup code�j

�I���W�i���̃\�[�X�R�[�h�͓ǂ݂ɂ����̂ŁA�N���[���i�b�v����K�v���������B
���ꂵ���R�}���h���C���p�[�X��A���������Ɋ֌W�̂Ȃ��p�P�b�g�p�[�X�͋ɗ͔r�������B
�m��K�v�͂Ȃ����A�p�P�b�g�p�[�X���ɗ́A�K�i���ɉ������p�b�N�hstruct�ŏ����������B

���ʁA���C���\�[�X�ECaption2Ass.cpp ��main, DumpAssLine, DumpSrtLine �݂̂��܂ށB
main �����������Ɋ֌W�̂Ȃ��R�[�h��r�������̂ŁA�����̗��ꂪ���Ղ��Ȃ��Ă���B

�I���W�i���ł̏����F

�I���W�i���ł�srt�o�͂���ړI�Ƃ��Ă���炵���A���C�����[�v�ł킴�킴UTF8�������
���X�g�𐶐����� DumpAssLine, DumpSrtLine �ɓn���Ă���i���̒i�K�ňʒu�E�F���Ȃ�
�������Ă���j�B

// parse caption;�@�ƃR�����g����Ă���Ƃ��납��A�����������n�܂�B

capUtil.AddTSPacket(pbPacket);�@��caption.dll�Ƀp�P�b�g�f�[�^���t�B�[�h���邪�A��x
�ł͕K�v�ȃf�[�^������Ȃ��B

capUtil.AddTSPacket��NO_ERR_CAPTION��Ԃ����Ƃ��AcapUtil.GetCaptionData��CAPTION_DATA
�𓾂邱�Ƃ��o����BGetCaptionData��CAPTION_DATA�̃x�N�^�[�^��Ԃ��B

����CAPTION_DATA�v�f��CAPTION_CHAR_DATA����strDecode.c_str()�ŁAcaption.dll�ɂ����ARIB
����sjis�ɕϊ����ꂽ����������o���AUTF8�ɕϊ�����std::list<PSRT_LINE> SRT_LIST���쐬
���Ă���B

CAPTION_DATA��bClear��true�̂Ƃ�������������dwWaitTime���m�肵�ADumpAssLine, 
DumpSrtLine ���Ăяo���Ă���B�������A���̂Ƃ���CAPTION_CHAR_DATA�̓��e�͖������Ă���B

��������ێ�����v��F

�ʒu�E�F�Ȃǂ̎�������CAPTION_DATA��CAPTION_CHAR_DATA�ɕێ�����Ă���̂ŁA������
�R�s�[�����X�g�i�܂��͔z��j�ɂ��āADumpAssLine, DumpSrtLine �̑��ŕ���������o���悤
�ɕύX����B��������ass/srt�ŃT�|�[�g�����^�O�̌`���ŏo�͂���B


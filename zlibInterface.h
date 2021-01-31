#ifndef ___ZLIBINTERFACE
#define ___ZLIBINTERFACE

// enum - Unzip Execute eXtract Return
enum {
	UZEXR_OK = 1,
	UZEXR_CANCEL = 0,
	// �}�C�i�X�l�͂��ׂăG���[�Ƃ��邱��
	UZEXR_INVALIDCALLBACK = -1,
	UZEXR_INVALIDFILE = -2,
	UZEXR_FATALERROR = -3
};

// enum - UnZip CallBack Return
enum {
	UZCBR_OK,
	UZCBR_PASS,
	UZCBR_CANCEL
};

// enum UnZip CallBack id
enum {
	UZCB_FIND_FILE,
	UZCB_EXTRACT_PROGRESS
};



// Unzip����̒ʒm���󂯎��R�[���o�b�N�֐��̃|�C���^���w�肷��
void Unzip_setCallback(int (*pfuncCallback)(int nCallbackId, unsigned long ulExtractSize,
		      unsigned long ulCurrentPosition, const void *pData,
                      unsigned long ulDataSize, unsigned long ulUserData));

// Unzip�Ɏw�肵���t�@�C���̉𓀂��܂��B
int Unzip_execExtract(const char *pszTargetFile, unsigned long ulUserData);


#endif //_ZLIBINTERFACE

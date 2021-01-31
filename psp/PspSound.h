/*
	Gives a perfect sound quality but also creates a slight tearing in the top of the screen, particularily visible with platform games.
	I think this is due to the fact that the multitasking used for the sound on PSP is associative, the main thread may not continue execution until the sound rendering thread has finished its job
	(and goes into sleep mode). The problem is that the sound thread will not stop when VBLANK is starting, and thus VBLANK may pass entirely without the CPU having time to 
*/
//#define GAME_BOY_PERFECT_SOUND

#ifdef __cplusplus
extern "C" {
#endif

void SoundInit(void);

void SoundUpdate(void);

void SoundPause(void);

void SoundResume(void);

void SoundTerm(void);

void SoundWaitTerm();

extern int soundPause;

#ifdef __cplusplus
}
#endif

#ifndef __SOUND_H__
#define __SOUND_H__

#ifdef __cplusplus
extern "C" {
#endif
extern char menuSoundTitle[128], menuSoundAlbum[128], menuSoundArtist[128];
int menuPlayZipSound(char *filename, int number);
void menuStartSound(char *filename);
void soundStandardVblank();
u32 ramAvailable (void);

void MenuSpecialMessageDisplay();
void menuMusicPause();
void menuMusicPlay();
void menuMusicStop();
void menuMusicPrevTrack();
void menuMusicNextTrack();
extern int menuSoundBoxActive;
extern int musicEnabled;
extern int menuMusicState;
#define MENU_SOUND_BOX_ACTIVE_MAX 180
//Delay for replaying the sound track when L is pressed (0=late - MENU_SOUND_BOX_ACTIVE_MAX=instant)
#define MENU_SOUND_BOX_PREVTRACK_TRESHOLD 0
#ifdef __cplusplus
}
#endif

#endif

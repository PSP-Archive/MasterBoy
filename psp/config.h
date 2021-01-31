#ifndef PSP_CONFIG_H
#define PSP_CONFIG_H

typedef struct		{
	char *nom;
	void *handle;
	unsigned char type, flags;
	void *handle2;
} IDENTIFICATEUR;

typedef struct		{
	int taille;
	const IDENTIFICATEUR *objet;
} INFOS_OBJET;

typedef struct		{
	int taille;
	int type;
	const void *tableau;
} INFOS_TABLEAU;

//Dans un tableau, toutes les chaînes ont cette taille!!!
#define TAILLE_MAX_CHAINES 512
enum {SAVETYPE_DEFAULT, SAVETYPE_GAME, SAVETYPE_CHANGESONLY};

extern int gblIndice;
extern char menuDefaultExecute[TAILLE_MAX_CHAINES];
extern MENUPARAMS *config_referenceConfig;

extern int ExecScript(VIRTUAL_FILE *f, char *fonction, char *command);
extern void SetVariableValue(const IDENTIFICATEUR *id, char *ptr);
extern char *GetVariableValue(const IDENTIFICATEUR *id, int withQuotes);
extern const IDENTIFICATEUR *GetVariableAddress(char *str);
extern int OuvreFichierConfig(char *nom_fichier, char *fonction);
extern void ScriptAjouteProcedures(char *nom_fichier, SUBMENU *s, char *startText);
extern char *GetChaine(char **dest);
extern u32 GetAddedColor(int color, int value);
extern void ScriptSave(const char *filename, int type);

#endif

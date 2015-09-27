/*
  Hatari - file.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_FILE_H
#define HATARI_FILE_H

extern void File_CleanFileName(char *pszFileName);
extern void File_AddSlashToEndFileName(char *pszFileName);
extern bool File_DoesFileExtensionMatch(const char *pszFileName, const char *pszExtension);
extern const char *File_RemoveFileNameDrive(const char *pszFileName);
extern bool File_DoesFileNameEndWithSlash(char *pszFileName);
extern off_t File_Length(const char *pszFileName);
extern bool File_Exists(const char *pszFileName);
extern bool File_DirExists(const char *psDirName);
extern bool File_QueryOverwrite(const char *pszFileName);
extern char* File_FindPossibleExtFileName(const char *pszFileName,const char * const ppszExts[]);
extern void File_SplitPath(char *pSrcFileName, char *pDir, char *pName, char *Ext);
extern char* File_MakePath(const char *pDir, const char *pName, const char *pExt);
extern void File_ShrinkName(char *pDestFileName, const char *pSrcFileName, int maxlen);
extern bool File_InputAvailable(FILE *fp);
extern void File_MakeAbsoluteSpecialName(char *pszFileName);
extern void File_MakeAbsoluteName(char *pszFileName);
extern void File_MakeValidPathName(char *pPathName);
extern void File_PathShorten(char *path, int dirs);
extern void File_HandleDotDirs(char *path);

#endif /* HATARI_FILE_H */

/*
 * I have an old Creative Nomad Jukebox MP3 player.  It still works, but I
 * can't easily change or retrieve the contents -- the software for this
 * doesn't work with modern operating systems, and is hard to find.  There is a
 * program for linux and macOS that is supposed to interface with the device,
 * but it's really slow and wasn't reliable for me.
 * 
 * This is a quick and dirty program to read song data from a disk image from a
 * Creative Nomad Jukebox MP3 player.  To use it, follow these steps:
 *
 * 1. Compile the program.
 * 3. Create a directory named jukebox-out.
 * 3. Create a disk image by removing the hard disk, and attaching it to a
 *    computer.  Use dd or your favorite disk imaging program to create an
 *    image of the disk.
 * 4. Name your disk image jukebox.img.
 * 5. Run the program.
 * 6. Your songs have been placed in the jukebox-out directory.
 *
 * If you don't understand any of the directions above, this program is not for
 * you.  Questions about how to compile or run the program will be ignored.
 * This program should compile and run on most UNIX and UNIX-like systems,
 * including Linux and macOS.  It can probably be compiled on Windows, with
 * Visual Studio, but the use of cygwin or mingw would be easier.
 *
 * The program displays a lot of information as it reads the disk image, mainly
 * for trouble-shooting purposes.  If you've added and deleted songs frequently
 * from the device, you'll probably notice more output than if you've only used
 * it a little.
 *
 * Because the program makes no attempt to understand which songs may have been
 * deleted, it might recover more than you expect, but be aware that some of
 * that "more than you expect" is probably corrupted.
 *
 * YMMV! The program worked to let me recover songs from my MP3 player, but it
 * might fail miserably for you.
 *
 * WARNING: Removing the hard disk from the MP3 player will void the MP3
 * player's warranty!
 */

/*
 * Copyright (c) 2018, Thomas J. Francis, Jr.
 * All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:

 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syslimits.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    const char *imageFileName = "jukebox.img";
    const char *outDirectory = "jukebox-out";
    const int readSize = 0x5f54;
    const int beginSize = 0x10;
    unsigned int fileCount = 0;
    unsigned char *buffer = (unsigned char *)calloc(readSize, 1);
    char fileName[PATH_MAX*2+2];

    printf("Saving songs to %s\n", outDirectory);

    if (buffer == NULL)
    {
	return 1;
    }

    FILE *fp = fopen(imageFileName, "rb");

    if (fp == NULL)
    {
	free(buffer);
	return 1;
    }

    do
    {
	if (fread(buffer, 1, readSize, fp) < readSize)
	{
	    printf("All done!  Created %u files!\n", fileCount);
	    break;
	}
	
	unsigned char *codec = (unsigned char *)memmem(buffer, readSize, "CODEC\0\0\0", 8);
	if (codec == NULL)
	{
	    fseek(fp, -7, SEEK_CUR);
	    continue;
	}
	off_t pos = (codec - buffer) + ftello(fp) - readSize;
	printf("Found header at: %llx...", pos);
	fflush(stdout);

	if (codec != (buffer + beginSize))
	{
	    printf("Repositioning so header is at start of buffer\n");
	    fflush(stdout);
	    fseek(fp, pos - beginSize, SEEK_SET);
	    continue;
	}

	unsigned char *nextCodec = (unsigned char *)memmem(buffer+beginSize+8, readSize-beginSize-8, "CODEC\0\0\0", 8);
        if (nextCodec != NULL)
	{
	    printf("Ignoring invalid header (contains header)\n");
	    fflush(stdout);
	    fseek(fp, -(readSize - ((nextCodec - buffer) - beginSize)), SEEK_CUR);
	    continue;
	}

	unsigned char *type = codec + 8;
	size_t bytesLeft = readSize - (type - buffer) - strlen((const char *)type);
	unsigned char *title = (unsigned char *)memmem(type+strlen((const char *)type), bytesLeft, "TITLE\0\0\0", 8);
	if (title == NULL)
	{
	    printf("Ignoring invalid header (missing TITLE)\n");
	    fflush(stdout);
	    nextCodec = (unsigned char *)memmem(type+strlen((const char *)type), bytesLeft, "CODEC\0\0\0", 8);
	    if (nextCodec != NULL)
	    {
		fseek(fp, -(readSize - ((nextCodec - buffer) - beginSize)), SEEK_CUR);
	    }
	    continue;
	}
	title += 8;
	snprintf(fileName, sizeof(fileName), "%s/%s.mp3", outDirectory, title);
	fileName[sizeof(fileName)-1] = '\0';
	// Make sure we don't overwrite an existing file!
	int counter = 2;
	while (access(fileName, F_OK) == 0)
	{
	    snprintf(fileName, sizeof(fileName), "%s/%s-%d.mp3", outDirectory, title, counter);
	    fileName[sizeof(fileName)-1] = '\0';
	}

	bytesLeft = readSize - (title - buffer) - strlen((const char *)title);

	unsigned char *fileSizePtr = (unsigned char *)memmem(title+strlen((const char *)title), bytesLeft, "FILE SIZE\0\0\0", 12);

	if (fileSizePtr == NULL)
	{
	    printf("Ignoring invalid header (missing FILE SIZE)\n");
	    nextCodec = (unsigned char *)memmem(type+strlen((const char *)type), bytesLeft, "CODEC\0\0\0", 8);
	    if (nextCodec != NULL)
	    {
		fseek(fp, -(readSize - ((nextCodec - buffer) - beginSize)), SEEK_CUR);
	    }
	    fflush(stdout);
	    continue;
	}
	fileSizePtr += 12;
	unsigned int fileSize = fileSizePtr[0] & 0xFF;
	fileSize += (fileSizePtr[1] & 0xFF) << 8;
	fileSize += (fileSizePtr[2] & 0xFF) << 16;
	fileSize += (fileSizePtr[3] & 0xFF) << 24;

	unsigned char *fileBuffer = (unsigned char *)malloc(fileSize);
	if (fileBuffer == NULL)
	{
	    printf("Out of memory!\n");
	    free(buffer);
	    fclose(fp);
	    return 1;
	}
	size_t bytesRead = fread(fileBuffer, 1, fileSize, fp);
	nextCodec = (unsigned char *)memmem(fileBuffer, fileSize, "\0\0CODEC\0\0\0", 12);
        if (nextCodec != NULL)
	{
	    printf("Ignoring invalid header (data contains header)\n");
	    fflush(stdout);
	    fseek(fp, -(fileSize - ((nextCodec - fileBuffer) - beginSize)), SEEK_CUR);
	    free(fileBuffer);
	    continue;
	}

	FILE *newFP = fopen(fileName, "wb");
	size_t bytesWritten = fwrite(fileBuffer, 1, bytesRead, newFP);
	free(fileBuffer);
	printf("Saved '%s': %lu of %u\n", title, bytesWritten, fileSize);
	fflush(stdout);
	fclose(newFP);
	fileCount++;

    } while (1);

    free(buffer);
    fclose(fp);

    return 0;
}

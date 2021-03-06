/*
 *	$Id: scsi-chio.c,v 1.14 2006/05/25 01:47:07 johnfranks Exp $
 *
 *	scsi-chio.c -- library routines to handle the changer
 *			support for chio based systems
 *
 *	Author: Eric Schnoebelen, eric@cirr.com
 *	based on work by: Larry Pyeatt,  pyeatt@cs.colostate.edu 
 *	Copyright: 1997, 1998 Eric Schnoebelen
 *		
 */

#include "config.h"
#include "amanda.h"
#include "scsi-defs.h"

#if (defined(HAVE_CHIO_H) || defined(HAVE_SYS_CHIO_H)) \
    && !defined(HAVE_CAMLIB_H)

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mtio.h>

/* This include comes with Gerd Knor's SCSI media changer driver.
 * If you are porting to another system, this is the file that defines
 * ioctl calls for the changer.  You will have to track it down yourself
 * and possibly change all the ioctl() calls in this program.  
 */

#if defined(HAVE_CHIO_H)
#  include <chio.h>
#else /* HAVE_SYS_CHIO_H must be defined */
#  include <sys/chio.h>
#endif

char *modname = "@(#)" __FILE__ 
		": SCSI support library for the chio(2) interface @(#)";

/*
 * cache the general changer information, for faster access elsewhere
 */
static struct changer_params changer_info;
static int changer_info_init = 0;

static int get_changer_info(fd)
{
int rc = 0;

    if ( !changer_info_init ) {
	rc = ioctl(fd, CHIOGPARAMS, &changer_info);
	changer_info_init++;
    }
    return (rc);
}

/* Get the number of the first free slot
 * return > 0 number of empty slot
 * return = 0 no slot free
 * return < 0 error
 */
int GetCurrentSlot(int fd, int drive)
{
    struct changer_element_status  ces;
    int slot;
    int i, rc;

    get_changer_info(fd);

    ces.ces_type = CHET_ST;
    ces.ces_data = malloc(changer_info.cp_nslots);

    rc = ioctl(fd, CHIOGSTATUS, &ces);
    if (rc) {
	dbprintf(("%s: changer status query failed: 0x%x %s\n",
			get_pname(), rc,strerror(errno)));
	return -1;
    }
    for (slot = 0; slot < changer_info.cp_nslots; slot++)
    {
    	i = ces.ces_data[slot] & CESTATUS_FULL;
    	dbprintf(("\tGetCurrentSlot slot %d = %d\n", slot, i));
    	if (!i)
            return(slot);
    }


}

int get_clean_state(int changerfd, char *changerdev, char *dev)
{
    return 0;

}
void eject_tape(char *tape)
/* This function ejects the tape from the drive */
{
int mtfd;
struct mtop mt_com;

    if ((mtfd = open(tape, O_RDWR)) < 0) {
        dbprintf(("eject_tape : failed\n"));
        perror(tape);
        exit(2);
    }
    mt_com.mt_op = MTOFFL;
    mt_com.mt_count = 1;
    if (ioctl(mtfd, MTIOCTOP, (char *)&mt_com) < 0) {
/*
    If the drive already ejected the tape due an error, or because it
    was a cleaning tape, threre can be an error, which we should ignore 

       perror(tape);
       exit(2);
*/
    }
    close(mtfd);
}


/* 
 * this routine checks a specified slot to see if it is empty 
 */
int isempty(int fd, int slot)
{
struct changer_element_status  ces;
int                            i,rc;
int type=CHET_ST;

    get_changer_info(fd);

    ces.ces_type = type;
    ces.ces_data = malloc(changer_info.cp_nslots);

    rc = ioctl(fd, CHIOGSTATUS, &ces);
    if (rc) {
	dbprintf(("%s: changer status query failed: 0x%x %s\n",
			get_pname(), rc,strerror(errno)));
	return -1;
    }

    i = ces.ces_data[slot] & CESTATUS_FULL;

    free(ces.ces_data);
    return !i;
}

/*
 * find the first empty slot 
 */
int find_empty(int fd, int start, int count)
{
struct changer_element_status  ces;
int                            i,rc;
int type=CHET_ST;

    get_changer_info(fd);

    ces.ces_type = type;
    ces.ces_data = malloc(changer_info.cp_nslots);

    rc = ioctl(fd,CHIOGSTATUS,&ces);
    if (rc) {
	dbprintf(("%s: changer status query failed: 0x%x %s\n",
			get_pname(), rc, strerror(errno)));
	return -1;
    }

    i = 0; 
    while ((i < changer_info.cp_nslots)&&(ces.ces_data[i] & CESTATUS_FULL))
	i++;
    free(ces.ces_data);
    return i;
}

/*
 * returns one if there is a tape loaded in the drive 
 */
int drive_loaded(int fd, int drivenum)
{
struct changer_element_status  ces;
int                            i,rc;
int type=CHET_DT;

    get_changer_info(fd);

    ces.ces_type = type;
    ces.ces_data = malloc(changer_info.cp_ndrives);

    rc = ioctl(fd, CHIOGSTATUS, &ces);
    if (rc) {
	dbprintf(("%s: drive status query failed: 0x%x %s\n",
			get_pname(), rc, strerror(errno)));
	return -1;
    }

    i = (ces.ces_data[drivenum] & CESTATUS_FULL);

    free(ces.ces_data);
    return i;
}


/*
 * unloads the drive, putting the tape in the specified slot 
 */
int unload(int fd, int drive, int slot)
{
struct changer_move  move;
int rc;

    dbprintf(("unload : fd = %d, drive = %d, slot =%d\n",fd, drive, slot));

    move.cm_fromtype = CHET_DT;
    move.cm_fromunit = drive;
    move.cm_totype = CHET_ST;
    move.cm_tounit = slot;
    move.cm_flags = 0;

    rc = ioctl(fd, CHIOMOVE, &move);
    if (rc){
	dbprintf(("%s: drive unload failed (MOVE): 0x%x %s\n",
		get_pname(), rc, strerror(errno)));
	return(-2);
    }
    return 0;
}


/*
 * moves tape from the specified slot into the drive 
 */
int load(int fd, int drive, int slot)
{
struct changer_move  move;
int rc;

    dbprintf(("load : fd = %d, drive = %d, slot =%d\n",fd, drive, slot));

    move.cm_fromtype = CHET_ST;
    move.cm_fromunit = slot;
    move.cm_totype = CHET_DT;
    move.cm_tounit = drive;
    move.cm_flags = 0;

    rc = ioctl(fd,CHIOMOVE,&move);
    if (rc){
	dbprintf(("%s: drive load failed (MOVE): 0x%x %s\n",
		get_pname(), rc, strerror(errno)));
	return(-2);
    }
    return(0);
}

int get_slot_count(int fd)
{ 
int rc;

    rc = get_changer_info(fd);
    if (rc) {
        dbprintf(("%s: slot count query failed: 0x%x %s\n", 
			get_pname(), rc, strerror(errno)));
        return -1;
    }

    return changer_info.cp_nslots;
}

int get_drive_count(int fd)
{ 
int rc;

    rc = get_changer_info(fd);
    if (rc) {
        dbprintf(("%s: drive count query failed: 0x%x %s\n",
			get_pname(), rc, strerror(errno)));
        return -1;
    }

    return changer_info.cp_ndrives;
}

/* This function should ask the drive if it is ready */
int Tape_Ready ( char *tapedev , int wait)
{
  FILE *out=NULL;
  int cnt=0;
  
  while ((cnt < wait) && (NULL==(out=fopen(tapedev,"w+")))){
    cnt++;
    sleep(1);
  }
  if (out != NULL)
    fclose(out);
  return 0;
}

int OpenDevice (char *tapedev)
{
  int DeviceFD;

  DeviceFD = open(tapedev, O_RDWR);
  return(DeviceFD);
}

int CloseDevice (char *device, int DeviceFD)
{
   int ret;

   dbprintf(("%s: CloseDevice(%s)\n", device, get_pname()));
   ret = close(DeviceFD);

   return ret;
}

#endif
/*
 * Local variables:
 * indent-tabs-mode: nil
 * c-default-style: gnu
 * End:
 */

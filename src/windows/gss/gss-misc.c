/*
 * Copyright 1994 by OpenVision Technologies, Inc.
 * 
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice appears in all copies and
 * that both that copyright notice and this permission notice appear in
 * supporting documentation, and that the name of OpenVision not be used
 * in advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission. OpenVision makes no
 * representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 * 
 * OPENVISION DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL OPENVISION BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF
 * USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR
 * OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */
#include "gss.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys\timeb.h>
#include <time.h>

FILE *display_file;
DWORD ws_err;

gss_buffer_desc empty_token_buf = { 0, (void *) "" };
gss_buffer_t empty_token = &empty_token_buf;

static void display_status_1
	(char *m, OM_uint32 code, int type);

static int write_all(int fildes, char *buf, unsigned int nbyte)
{
    int ret;
    char *ptr;

    for (ptr = buf; nbyte; ptr += ret, nbyte -= ret) {
        ret = send(fildes, ptr, nbyte, 0);
        if (ret < 0) {
            ws_err = WSAGetLastError();
            errno = ws_err;
            return(ret);
        } else if (ret == 0) {
            return(ptr-buf);
        }
    }

    return(ptr-buf);
}

static int read_all(int s, char *buf, unsigned int nbyte)
{
    int ret;
    char *ptr;
    fd_set rfds;
    struct timeval tv;

    FD_ZERO(&rfds);
    FD_SET(s, &rfds);
    tv.tv_sec = 10;
    tv.tv_usec = 0;

    for (ptr = buf; nbyte; ptr += ret, nbyte -= ret) {
        if ( select(FD_SETSIZE, &rfds, NULL, NULL, &tv) <= 0 || !FD_ISSET(s, &rfds) )
            return(ptr-buf);
        ret = recv(s, ptr, nbyte, 0);
        if (ret < 0) {
            ws_err = WSAGetLastError();
            errno = ws_err;
            return(ret);
        } else if (ret == 0) {
            return(ptr-buf);
        }
    }

    return(ptr-buf);
}

/*
 * Function: send_token
 *
 * Purpose: Writes a token to a file descriptor.
 *
 * Arguments:
 *
 * 	s		(r) an open file descriptor
 *	flags		(r) the flags to write
 * 	tok		(r) the token to write
 *
 * Returns: 0 on success, -1 on failure
 *
 * Effects:
 *
 * If the flags are non-null, send_token writes the token flags (a
 * single byte, even though they're passed in in an integer). Next,
 * the token length (as a network long) and then the token data are
 * written to the file descriptor s.  It returns 0 on success, and -1
 * if an error occurs or if it could not write all the data.
 */
int send_token(int s, int flags, gss_buffer_t tok)
{
     int len, ret;
     unsigned char char_flags = (unsigned char) flags;
     unsigned char lenbuf[4];

     if (char_flags) {
         ret = write_all(s, (char *)&char_flags, 1);
         if (ret != 1) {
             my_perror("sending token flags");
             OkMsgBox ("Winsock error  %d \n", ws_err);
             return -1;
         }
     }
    if (tok->length > 0xffffffffUL)
        abort();
    lenbuf[0] = (tok->length >> 24) & 0xff;
    lenbuf[1] = (tok->length >> 16) & 0xff;
    lenbuf[2] = (tok->length >> 8) & 0xff;
    lenbuf[3] = tok->length & 0xff;

    ret = write_all(s, lenbuf, 4);
    if (ret < 0) {
        my_perror("sending token length");
		OkMsgBox ("Winsock error  %d \n", ws_err);
        return -1;
    } else if (ret != 4) {
        if (verbose)
            printf("sending token length: %d of %d bytes written\r\n", 
                     ret, 4);
        return -1;
    }

    ret = write_all(s, tok->value, tok->length);
    if (ret < 0) {
        my_perror("sending token data");
		OkMsgBox ("Winsock error  %d \n", ws_err);
        return -1;
    } else if (ret != tok->length) {
        if (verbose)
            printf("sending token data: %d of %d bytes written\r\n", 
                     ret, (int) tok->length);
        return -1;
    }

    return 0;
}

/*
 * Function: recv_token
 *
 * Purpose: Reads a token from a file descriptor.
 *
 * Arguments:
 *
 * 	s		(r) an open file descriptor
 *	flags		(w) the read flags
 * 	tok		(w) the read token
 *
 * Returns: 0 on success, -1 on failure
 *
 * Effects:
 * 
 * recv_token reads the token flags (a single byte, even though
 * they're stored into an integer, then reads the token length (as a
 * network long), allocates memory to hold the data, and then reads
 * the token data from the file descriptor s.  It blocks to read the
 * length and data, if necessary.  On a successful return, the token
 * should be freed with gss_release_buffer.  It returns 0 on success,
 * and -1 if an error occurs or if it could not read all the data.
 */
int recv_token(int s, int * flags, gss_buffer_t tok)
{
    int ret;
    unsigned char char_flags;
    unsigned char lenbuf[4];

    ret = read_all(s, (char *) &char_flags, 1);
    if (ret < 0) {
        my_perror("reading token flags");
		OkMsgBox ("Winsock error  %d \n", ws_err);
        return -1;
    } else if (! ret) {
        if (display_file)
            printf("reading token flags: 0 bytes read\r\n", display_file);
        return -1;
    } else {
        *flags = (int) char_flags;
    }

    if (char_flags == 0 ) {
        lenbuf[0] = 0;
        ret = read_all(s, &lenbuf[1], 3);
        if (ret < 0) {
            my_perror("reading token length");
            OkMsgBox ("Winsock error  %d \n", ws_err);
            return -1;
        } else if (ret != 3) {
            if (verbose)
                printf("reading token length: %d of %d bytes read\r\n", 
                         ret, 3);
            return -1;
        }
    }
    else {
        ret = read_all(s, lenbuf, 4);
        if (ret < 0) {
            my_perror("reading token length");
            OkMsgBox ("Winsock error  %d \n", ws_err);
            return -1;
        } else if (ret != 4) {
            if (verbose)
                printf("reading token length: %d of %d bytes read\r\n", 
                         ret, 4);
            return -1;
        }
    }

    tok->length = ((lenbuf[0] << 24)
                    | (lenbuf[1] << 16)
                    | (lenbuf[2] << 8)
                    | lenbuf[3]);
    tok->value = (char *) malloc(tok->length ? tok->length : 1);
    if (tok->length && tok->value == NULL) {
        if (verbose)
            printf("Out of memory allocating token data\r\n");
        return -1;
    }

    ret = read_all(s, (char *) tok->value, tok->length);
    if (ret < 0) {
        my_perror("reading token data");
		OkMsgBox ("Winsock error  %d \n", ws_err);
        free(tok->value);
        return -1;
    } else if (ret != tok->length) {
        printf("sending token data: %d of %d bytes written\r\n", 
                 ret, (int) tok->length);
        free(tok->value);
        return -1;
    }

    return 0;
}

void 
free_token(gss_buffer_t tok)
{
    if (tok->length <= 0 || tok->value == NULL)
        return;

    free(tok->value);
    tok->value = NULL;
    tok->length = 0;
}

/*+
 * Function: display_status
 *
 * Purpose: displays GSS-API messages
 *
 * Arguments:
 *
 *	msg		a string to be displayed with the message
 *	maj_stat	the GSS-API major status code
 *	min_stat	the GSS-API minor status code
 *
 * Effects:
 *
 * The GSS-API messages associated with maj_stat and min_stat are
 * displayed on stderr, each preceeded by "GSS-API error <msg>: " and
 * followed by a newline.
 */
void
display_status (char *msg, OM_uint32 maj_stat, OM_uint32 min_stat) {
    display_status_1(msg, maj_stat, GSS_C_GSS_CODE);
    display_status_1(msg, min_stat, GSS_C_MECH_CODE);
}

static void
display_status_1(char *m, OM_uint32 code, int type) {
    OM_uint32 maj_stat, min_stat;
    gss_buffer_desc msg;
    OM_uint32 msg_ctx;
     
    msg_ctx = 0;
    while (1) {
        maj_stat = gss_display_status(&min_stat, code,
                                      type, GSS_C_NULL_OID,
                                      &msg_ctx, &msg);
        if (verbose)
            printf("GSS-API error %s: %s\r\n", m,
                     (char *)msg.value); 
        OkMsgBox ("GSS-API error %s: %s\n", m,
            (char *)msg.value);
        (void) gss_release_buffer(&min_stat, &msg);
	  
        if (!msg_ctx)
            break;
    }
}

/*
 * Function: display_ctx_flags
 *
 * Purpose: displays the flags returned by context initation in
 *	    a human-readable form
 *
 * Arguments:
 *
 * 	int		ret_flags
 *
 * Effects:
 *
 * Strings corresponding to the context flags are printed on
 * stdout, preceded by "context flag: " and followed by a newline
 */

void display_ctx_flags(flags)
     OM_uint32 flags;
{
     if (flags & GSS_C_DELEG_FLAG)
	  printf("context flag: GSS_C_DELEG_FLAG\r\n");
     if (flags & GSS_C_MUTUAL_FLAG)
	  printf("context flag: GSS_C_MUTUAL_FLAG\r\n");
     if (flags & GSS_C_REPLAY_FLAG)
	  printf("context flag: GSS_C_REPLAY_FLAG\r\n");
     if (flags & GSS_C_SEQUENCE_FLAG)
	  printf("context flag: GSS_C_SEQUENCE_FLAG\r\n");
     if (flags & GSS_C_CONF_FLAG )
	  printf("context flag: GSS_C_CONF_FLAG \r\n");
     if (flags & GSS_C_INTEG_FLAG )
	  printf("context flag: GSS_C_INTEG_FLAG \r\n");
}

void print_token(tok)
     gss_buffer_t tok;
{
    int i;
    unsigned char *p = tok->value;

    if (!verbose)
	return;
    for (i=0; i < tok->length; i++, p++) {
	printf("%02x ", *p);
	if ((i % 16) == 15) {
	    printf("\r\n");
	}
    }
    printf("\r\n");
}


int gettimeofday (struct timeval *tv, void *ignore_tz)
{
    struct _timeb tb;
    _tzset();
    _ftime(&tb);
    if (tv) {
	tv->tv_sec = tb.time;
	tv->tv_usec = tb.millitm * 1000;
    }
    return 0;
}

/*+*************************************************************************
** 
** OkMsgBox
** 
** A MessageBox version of printf
** 
***************************************************************************/
void
OkMsgBox (char *format, ...) {
    char buf[256];								// Message goes into here
    char *args;                                 // Args for printf

    args = (char *) &format + sizeof(format);
    vsprintf (buf, format, args);
    MessageBox(NULL, buf, "", MB_OK);
}
/*+*************************************************************************
** 
** My_perror
** 
** A windows conversion of perror displaying the output into a MessageBox.
** 
***************************************************************************/
void
my_perror (char *msg) {
    char *err;

    err = strerror (errno);

    if (msg && *msg != '\0') 
        OkMsgBox ("%s: %s", msg, err);
    else
        MessageBox (NULL, err, "", MB_OK);
}


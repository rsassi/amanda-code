/*
 * Copyright (c) 2005 Zmanda, Inc.  All Rights Reserved.
 * 
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License version 2.1 as 
 * published by the Free Software Foundation.
 * 
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA.
 * 
 * Contact information: Zmanda Inc., 505 N Mathlida Ave, Suite 120
 * Sunnyvale, CA 94085, USA, or: http://www.zmanda.com
 */

#ifndef __S3_H__
#define __S3_H__
#include <glib.h>
#include <curl/curl.h>

/*
 * Data types
 */

/* An opaque handle.  S3Handles should only be accessed from a single
 * thread at any given time, although it is fine to use different handles
 * in different threads simultaneously. */
typedef struct S3Handle S3Handle;

/*
 * Constants
 */

#ifdef WANT_DEVPAY
/* These are assumed to be already URL-escaped. */
# define STS_BASE_URL "https://sts.amazonaws.com/"
# define STS_PRODUCT_TOKEN "{ProductToken}AAAGQXBwVGtu4geoGybuwuk8VEEPzJ9ZANpu0yzbf9g4Gs5Iarzff9B7qaDBEEaWcAzWpcN7zmdMO765jOtEFc4DWTRNkpPSzUnTdkHbdYUamath73OreaZtB86jy/JF0gsHZfhxeKc/3aLr8HNT//DsX3r272zYHLDPWWUbFguOwqNjllnt6BshYREx59l8RrWABLSa37dyJeN+faGvz3uQxiDakZRn3LfInOE6d9+fTFl50LPoP08LCqI/SJfpouzWix7D/cep3Jq8yYNyM1rgAOTF7/wh7r8OuPDLJ/xZUDLfykePIAM="
#endif

/* This preprocessor magic will enumerate constants named S3_ERROR_XxxYyy for
 * each of the errors in parentheses.
 *
 * see http://docs.amazonwebservices.com/AmazonS3/2006-03-01/ErrorCodeList.html
 * for Amazon's breakdown of error responses.
 */
#define S3_ERROR_LIST \
    S3_ERROR(None), \
    S3_ERROR(AccountProblem), \
    S3_ERROR(AllAccessDisabled), \
    S3_ERROR(AmbiguousGrantByEmailAddress), \
    S3_ERROR(OperationAborted), \
    S3_ERROR(BadDigest), \
    S3_ERROR(BucketAlreadyExists), \
    S3_ERROR(BucketNotEmpty), \
    S3_ERROR(CredentialsNotSupported), \
    S3_ERROR(EntityTooLarge), \
    S3_ERROR(IncompleteBody), \
    S3_ERROR(InternalError), \
    S3_ERROR(InvalidAccessKeyId), \
    S3_ERROR(InvalidArgument), \
    S3_ERROR(InvalidBucketName), \
    S3_ERROR(InvalidDigest), \
    S3_ERROR(InvalidRange), \
    S3_ERROR(InvalidSecurity), \
    S3_ERROR(InvalidSOAPRequest), \
    S3_ERROR(InvalidStorageClass), \
    S3_ERROR(InvalidTargetBucketForLogging), \
    S3_ERROR(KeyTooLong), \
    S3_ERROR(InvalidURI), \
    S3_ERROR(MalformedACLError), \
    S3_ERROR(MaxMessageLengthExceeded), \
    S3_ERROR(MetadataTooLarge), \
    S3_ERROR(MethodNotAllowed), \
    S3_ERROR(MissingAttachment), \
    S3_ERROR(MissingContentLength), \
    S3_ERROR(MissingSecurityElement), \
    S3_ERROR(MissingSecurityHeader), \
    S3_ERROR(NoLoggingStatusForKey), \
    S3_ERROR(NoSuchBucket), \
    S3_ERROR(NoSuchKey), \
    S3_ERROR(NotImplemented), \
    S3_ERROR(NotSignedUp), \
    S3_ERROR(PreconditionFailed), \
    S3_ERROR(RequestTimeout), \
    S3_ERROR(RequestTimeTooSkewed), \
    S3_ERROR(RequestTorrentOfBucketError), \
    S3_ERROR(SignatureDoesNotMatch), \
    S3_ERROR(TooManyBuckets), \
    S3_ERROR(UnexpectedContent), \
    S3_ERROR(UnresolvableGrantByEmailAddress), \
    S3_ERROR(Unknown), \
    S3_ERROR(END)

typedef enum {
#define S3_ERROR(NAME) S3_ERROR_ ## NAME
    S3_ERROR_LIST
#undef S3_ERROR
} s3_error_code_t;

/*
 * Functions
 */

/* Initialize S3 operation
 *
 * As a requirement of C{curl_global_init}, which this function calls,
 * s3_init I{must} be called before any other threads are started.
 *
 * If an error occurs in this function, diagnostic information is 
 * printed to stderr.
 *
 * @returns: false if an error occurred
 */
gboolean
s3_init(void);

/* Set up an S3Handle.
 */
S3Handle *
s3_open(const char * access_key, const char *secret_key
#ifdef WANT_DEVPAY
        , const char * user_token
#endif
        );

/* Deallocate an S3Handle
 *
 * @param hdl: the S3Handle object
 */
void
s3_free(S3Handle *hdl);

/* Reset the information about the last request, including
 * freeing any allocated memory.  The S3Handle itself is not
 * freed and may be used again.  This function is called
 * automatically as needed, and should be called to free memory
 * when the handle will not be used for some time.
 *
 * @param hdl: the S3Handle object
 */
void
s3_reset(S3Handle *hdl);

/* Get the error information for the last operation
 *
 * All results are returned via result parameters.  If any parameter is
 * NULL, that result will not be returned.  Caller is not responsible for
 * freeing any returned strings, although the results are only valid until
 * the next call to an S3 function with this handle.
 * 
 * @param hdl: the S3Handle object
 * @param message: (result) the error message, or NULL if none exists
 * @param response_code: (result) the HTTP response code (or 0 if none exists)
 * @param s3_error_code: (result) the S3 error code (see constants, above)
 * @param s3_error_name: (result) the S3 error name (e.g., "RequestTimeout"),
 * or NULL if none exists
 * @param curl_code: (result) the curl error code (or 0 if none exists)
 * @param num_retries: (result) number of retries
 */
void
s3_error(S3Handle *hdl,
         const char **message,
         guint *response_code,
         s3_error_code_t *s3_error_code,
         const char **s3_error_name,
         CURLcode *curl_code,
         guint *num_retries);

/* Control verbose output of HTTP transactions, etc.
 *
 * @param hdl: the S3Handle object
 * @param verbose: if true, send HTTP transactions, etc. to debug output
 */
void
s3_verbose(S3Handle *hdl,
	   gboolean verbose);

/* Get the error information from the last operation on this handle,
 * formatted as a string.
 *
 * Caller is responsible for freeing the resulting string.
 *
 * @param hdl: the S3Handle object
 * @returns: string, or NULL if no error occurred
 */
char *
s3_strerror(S3Handle *hdl);

/* Perform an upload.
 * 
 * When this function returns, KEY and BUFFER remain the
 * responsibility of the caller.
 *
 * @param hdl: the S3Handle object
 * @param bucket: the bucket to which the upload should be made
 * @param key: the key to which the upload should be made
 * @param buffer: the data to be uploaded
 * @param buffer_len: the length of the data to upload
 * @returns: false if an error ocurred
 */
gboolean
s3_upload(S3Handle *hdl,
          const char *bucket,
          const char *key, 
          gpointer buffer,
          guint buffer_len);

/* List all of the files matching the pseudo-glob C{PREFIX*DELIMITER*}, 
 * returning only that portion which matches C{PREFIX*DELIMITER}.  S3 supports
 * this particular semantics, making it quite efficient.  The returned list
 * should be freed by the caller.
 *
 * @param hdl: the S3Handle object
 * @param bucket: the bucket to list
 * @param prefix: the prefix
 * @param delimiter: delimiter (any length string)
 * @param list: (output) the list of files
 * @returns: FALSE if an error occurs
 */
gboolean
s3_list_keys(S3Handle *hdl,
              const char *bucket,
              const char *prefix,
              const char *delimiter,
              GSList **list);

/* Read an entire file.  The buffer returned is the responsibility of the caller.  A
 * buffer is only returned if no error occurred, and will be NULL otherwise.
 *
 * @param hdl: the S3Handle object
 * @param bucket: the bucket to read from
 * @param key: the key to read from
 * @param buf_ptr: (result) a pointer to a C{gpointer} which will contain a pointer to
 * the block read
 * @param buf_size: (result) a pointer to a C{guint} which will contain the size of the
 * block read
 * @param max_size: maximum size of the file
 * @returns: FALSE if an error occurs
 */
gboolean
s3_read(S3Handle *hdl,
        const char *bucket,
        const char *key,
        gpointer *buf_ptr,
        guint *buf_size,
        guint max_size);

/* Delete a file.
 *
 * @param hdl: the S3Handle object
 * @param bucket: the bucket to delete from
 * @param key: the key to delete
 * @returns: FALSE if an error occurs; a non-existent file is I{not} considered an error.
 */
gboolean
s3_delete(S3Handle *hdl,
          const char *bucket,
          const char *key);

/* Create a bucket.
 *
 * @param hdl: the S3Handle object
 * @param bucket: the bucket to create
 * @returns: FALSE if an error occurs
 */
gboolean
s3_make_bucket(S3Handle *hdl,
               const char *bucket);

/* Attempt a RefreshAWSSecurityToken on a token; if it succeeds, the old
 * token will be freed and replaced by the new. If it fails, the old
 * token is left unchanged and FALSE is returned. */
gboolean sts_refresh_token(char ** token, const char * directory);

/* These functions are for if you want to use curl on your own. You get more
 * control, but it's a lot of work that way: */
typedef struct {
    char *buffer;
    guint buffer_len;
    guint buffer_pos;
    guint max_buffer_size;
} CurlBuffer;

/* a CURLOPT_READFUNCTION to read data from a buffer. */
size_t buffer_readfunction(void *ptr, size_t size,
                           size_t nmemb, void * stream);

/* a CURLOPT_WRITEFUNCTION to write data to a buffer. */
size_t
buffer_writefunction(void *ptr, size_t size, size_t nmemb, void *stream);

/* Adds a null termination to a buffer. */
void terminate_buffer(CurlBuffer *);

#endif

/*
  Common functions used by the NSS-aware code in systemtap.

  Copyright (C) 2009-2018 Red Hat Inc.

  This file is part of systemtap, and is free software.  You can
  redistribute it and/or modify it under the terms of the GNU General Public
  License as published by the Free Software Foundation; either version 2 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "config.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cerrno>
#include <cstdio>
#include <cassert>

extern "C" {
#include <time.h>
#include <termios.h>
#include <unistd.h>
#include <glob.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <nss.h>
#include <nspr.h>
#include <ssl.h>
#include <prerror.h>
#include <secerr.h>
#include <sslerr.h>
#include <cryptohi.h>
#include <keyhi.h>
#include <secder.h>
#include <cert.h>
#ifdef HAVE_HTTP_SUPPORT
#include <openssl/bio.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#endif
}

#include "nsscommon.h"
#include "staputil.h"

using namespace std;

// Common constants and settings.
const char *
server_cert_nickname ()
{
  return (const char *)"stap-server";
}

string
add_cert_db_prefix (const string &db_path) {
#if (NSS_VMAJOR > 3) || (NSS_VMAJOR == 3 && NSS_VMINOR >= 55)
  // https://wiki.mozilla.org/NSS_Shared_DB
  if (db_path.find (':') == string::npos)
    return string("sql:") + db_path;
#elif (NSS_VMAJOR > 3) || (NSS_VMAJOR == 3 && NSS_VMINOR >= 12)
  // Use the dbm prefix, if a prefix is not already specified,
  // since we're using the old database format.
  if (db_path.find (':') == string::npos)
    return string("dbm:") + db_path;
#endif
  return db_path;
}

string
server_cert_db_path ()
{
  string data_path;
  const char* s_d = getenv ("SYSTEMTAP_DIR");
  if (s_d != NULL)
    data_path = s_d;
  else
    data_path = get_home_directory() + string("/.systemtap");
  return data_path + "/ssl/server";
}

string
local_client_cert_db_path ()
{
  string data_path;
  const char* s_d = getenv ("SYSTEMTAP_DIR");
  if (s_d != NULL)
    data_path = s_d;
  else
    data_path = get_home_directory() + string("/.systemtap");
  return data_path + "/ssl/client";
}

// Common error handling for applications using this file.
void
nsscommon_error (const string &msg, int logit)
{
  // Call the extern "C" version supplied by each application.
  nsscommon_error (msg.c_str (), logit);
}

// Logging. Enabled only by stap-serverd but called from some common methods.
static ofstream logfile;

void
start_log (const char *arg, bool redirect_clog)
{
  if (logfile.is_open ())
    logfile.close ();

  logfile.open (arg, ios_base::app);
  if (! logfile.good ())
    nsscommon_error (_F("Could not open log file %s", arg));
  else if (redirect_clog)
    clog.rdbuf(logfile.rdbuf());
}

bool
log_ok ()
{
  return logfile.good ();
}

void
log (const string &msg)
{
  // What time is it?
  time_t now;
  time (& now);
  string nowStr = ctime (& now);
  // Remove the newline from the end of the time string.
  nowStr.erase (nowStr.size () - 1, 1);

  if (logfile.good ())
    logfile << nowStr << ": " << msg << endl << flush;
  else
    clog << nowStr << ": " << msg << endl << flush;
}

void
end_log ()
{
  if (logfile.is_open ())
    logfile.close ();
}

// NSS/NSPR error reporting and cleanup.
// These functions are called from C code as well as C++, so make them extern "C".
extern "C"
void
nssError (void)
{
  // See if PR_GetError can tell us what the error is.
  PRErrorCode errorNumber = PR_GetError ();

   // Try PR_ErrorToName and PR_ErrorToString; they are wise.
   const char* errorName = PR_ErrorToName(errorNumber);
   const char* errorText = PR_ErrorToString (errorNumber, PR_LANGUAGE_EN);

   if (errorName && errorText)
     {
       nsscommon_error (_F("(%d %s) %s", errorNumber, errorName, errorText));
       return;
     }

  errorText = "Unknown error";
  nsscommon_error (_F("(%d) %s", errorNumber, errorText));
}

extern "C"
NSSInitContext*
nssInitContext (const char *db_path, int readWrite, int issueMessage)
{
  NSSInitContext *context;
  PRUint32 flags = (readWrite ? NSS_INIT_READONLY : 0) | NSS_INIT_OPTIMIZESPACE;

  string full_db_path = add_cert_db_prefix (db_path);
  db_path = full_db_path.c_str();
  // DEBUG
  char *sd = getenv("SYSTEMTAP_DIR");
  string nssil_tmp = "/tmp/nssinit.log";
  std::ofstream nssil_out(nssil_tmp, std::ofstream::app);
  nssil_out << "NI db=" << db_path << " env=" << (sd ? sd : "") << (readWrite ? " rw" : " r") << endl;
  nssil_out.close();
  // DEBUG

  context = NSS_InitContext (db_path, "", "", SECMOD_DB, NULL, flags);
  if (context == NULL && issueMessage)
    {
      nsscommon_error (_F("Error initializing NSS for %s", db_path));
      nssError ();
    }
  return context;
}

extern "C"
SECStatus
nssInit (const char *db_path, int readWrite, int issueMessage)
{
  SECStatus secStatus;
  string full_db_path = add_cert_db_prefix (db_path);
  db_path = full_db_path.c_str();
  
  if (readWrite)
    secStatus = NSS_InitReadWrite (db_path);
  else
    secStatus = NSS_Init (db_path);
  if (secStatus != SECSuccess && issueMessage)
    {
      nsscommon_error (_F("Error initializing NSS for %s", db_path));
      nssError ();
    }
  return secStatus;
}

extern "C"
void
nssCleanup (const char *db_path, NSSInitContext *context)
{
  // Make sure that NSS has been initialized. Some early versions of NSS do not check this
  // within NSS_Shutdown().
  // When called with no certificate database path (db_path == 0), then the caller does
  // not know whether NSS has actually been initialized. For example, the rpm finder,
  // which calls here to shutdown NSS manually if rpmFreeCrypto() is not available
  // (see rpm_finder.cxx:missing_rpm_enlist).
  // However, if we're trying to close a certificate database which has not been initialized,
  // then we have a (non fatal) internal error.
  if (! NSS_IsInitialized ())
    {
      if (db_path)
	{
	  string full_db_path = add_cert_db_prefix (db_path);
	  db_path = full_db_path.c_str();
	  nsscommon_error (_F("WARNING: Attempt to shutdown NSS for database %s, which was never initialized", db_path));
	}
      return;
    }

  // Shutdown NSS and ensure that it went down successfully. This is because we can not
  // initialize NSS again if it does not.

  SECStatus secStatus;
  if (context != NULL)
    secStatus = NSS_ShutdownContext (context);
  else
    secStatus = NSS_Shutdown ();
  if (secStatus != SECSuccess)
    {
      if (db_path)
	{
	  string full_db_path = add_cert_db_prefix (db_path);
	  db_path = full_db_path.c_str();
	  nsscommon_error (_F("Unable to shutdown NSS for database %s", db_path));
	}
      else
	nsscommon_error (_("Unable to shutdown NSS"));
      nssError ();
    }
}

// Certificate database password support functions.
//
// Disable character echoing, if the fd is a tty.
static void
echoOff(int fd)
{
  if (isatty(fd)) {
    struct termios tio;
    tcgetattr(fd, &tio);
    tio.c_lflag &= ~ECHO;
    tcsetattr(fd, TCSAFLUSH, &tio);
  }
}

/* Enable character echoing, if the fd is a tty.  */
static void
echoOn(int fd)
{
  if (isatty(fd)) {
    struct termios tio;
    tcgetattr(fd, &tio);
    tio.c_lflag |= ECHO;
    tcsetattr(fd, TCSAFLUSH, &tio);
  }
}

/*
 * This function is our custom password handler that is called by
 * NSS when retrieving private certs and keys from the database. Returns a
 * pointer to a string with a password for the database. Password pointer
 * must be allocated by one of the NSPR memory allocation functions, or by PORT_Strdup,
 * and will be freed by the caller.
 */
extern "C"
char *
nssPasswordCallback (PK11SlotInfo *info __attribute ((unused)), PRBool retry, void *arg)
{
  static int retries = 0;
  #define PW_MAX 200
  char* password = NULL;
  char* password_ret = NULL;
  const char *dbname ;
  int infd;
  int isTTY;

  if (! retry)
    {
      /* Not a retry. */
      retries = 0;
    }
  else
    {
      /* Maximum of 2 retries for bad password.  */
      if (++retries > 2)
	return NULL; /* No more retries */
    }

  /* Can only prompt for a password if stdin is a tty.  */
  infd = fileno (stdin);
  isTTY = isatty (infd);
  if (! isTTY)
    {
      nsscommon_error (_("Cannot prompt for certificate database password. stdin is not a tty"));
      return NULL;
    }

  /* Prompt for password */
  password = (char *)PORT_Alloc (PW_MAX);
  if (! password)
    {
      nssError ();
      return NULL;
    }

  dbname = (const char *)arg;
  cerr << _F("Password for certificate database in %s: ", dbname) << flush;
  echoOff (infd);
  password_ret = fgets (password, PW_MAX, stdin);
  cerr << endl << flush;
  echoOn(infd);

  if (password_ret)
    /* stomp on the newline */
    *strchrnul (password, '\n') = '\0';
  else
    PORT_Free (password);

  return password_ret;
}

static int
create_server_cert_db (const char *db_path)
{
  return create_dir (db_path, 0755);
}

static int
create_client_cert_db (const char *db_path)
{
  // Same properties as the server's database, at present.
  return create_server_cert_db (db_path);
}

static int
clean_cert_db (const string &db_path)
{
  // First remove all files from the directory
  glob_t globbuf;
  string filespec = db_path + "/*";
  int r = glob (filespec.c_str (), 0, NULL, & globbuf);
  if (r == GLOB_NOSPACE || r == GLOB_ABORTED)
    nsscommon_error (_F("Could not search certificate database directory %s", db_path.c_str ()));
  else if (r != GLOB_NOMATCH)
    {
      for (unsigned i = 0; i < globbuf.gl_pathc; ++i)
	{
	  if (remove_file_or_dir (globbuf.gl_pathv[i]) != 0)
	    nsscommon_error (_F("Could not remove %s", globbuf.gl_pathv[i]));
	}
      globfree(& globbuf);
    }

  // Now remove the directory itself.
  if (remove_file_or_dir (db_path.c_str ()) != 0)
    {
      nsscommon_error (_F("Could not remove certificate database directory %s\n%s",
			  db_path.c_str (), strerror (errno)));
      return 1;
    }

  return 0;
}

static int
init_password (PK11SlotInfo *slot, const string &db_path, bool use_password)
{
  // Prompt for the database password, if we're using one. Keep the passwords in memory for as
  // little time as possible.
  SECStatus secStatus;
  if (use_password)
    {
      char *pw1 = 0;
      int attempts;
      const int max_attempts = 3;
      for (attempts = 0; attempts < max_attempts; ++attempts)
	{
	  pw1 = nssPasswordCallback (slot, false, (void*)db_path.c_str ());
	  if (! pw1)
	    continue;
	  cerr << "Confirm ";
	  bool match = false;
	  char *pw2 = nssPasswordCallback (slot, false, (void*)db_path.c_str ());
	  if (pw2)
	    {
	      if (strcmp (pw1, pw2) == 0)
		match = true;
	      else
		nsscommon_error (_("Passwords do not match"));
	      memset (pw2, 0, strlen (pw2));
	      PORT_Free (pw2);
	    }
	  if (match)
	    break;
	  memset (pw1, 0, strlen (pw1));
	  PORT_Free (pw1);
	}
      if (attempts >= max_attempts)
	{
	  nsscommon_error (_("Too many password attempts"));
	  return 1;
	}
      secStatus = PK11_InitPin (slot, 0, pw1);
      memset (pw1, 0, strlen (pw1));
      PORT_Free (pw1);
    }
  else
    secStatus = PK11_InitPin (slot, 0, 0);

  if (secStatus != SECSuccess)
    {
      nsscommon_error (_F("Could not initialize pin for certificate database %s", db_path.c_str()));
      nssError ();
      return 1;
    }

  return 0;
}

static SECKEYPrivateKey *
generate_private_key (const string &db_path, PK11SlotInfo *slot, SECKEYPublicKey **pubkeyp)
{
  if (PK11_Authenticate (slot, PR_TRUE, 0) != SECSuccess)
    {
      nsscommon_error (_F("Unable to authenticate the default slot for certificate database %s",
			  db_path.c_str ()));
      nssError ();
      return 0;
    }

  // Do some random-number initialization.
  // TODO: We can do better.
  srand (time (NULL));
  char randbuf[64];
  for (unsigned i = 0; i < sizeof (randbuf); ++i)
    randbuf[i] = rand ();
  PK11_RandomUpdate (randbuf, sizeof (randbuf));
  memset (randbuf, 0, sizeof (randbuf));

  // Set up for RSA.
  PK11RSAGenParams rsaparams;
  rsaparams.keySizeInBits = 4096; /* 1024 too small; SEC_ERROR_CERT_SIGNATURE_ALGORITHM_DISABLED */
  rsaparams.pe = 0x010001;
  CK_MECHANISM_TYPE mechanism = CKM_RSA_PKCS_KEY_PAIR_GEN;

  // Generate the key pair.
  SECKEYPrivateKey *privKey = PK11_GenerateKeyPair (slot, mechanism, & rsaparams, pubkeyp,
						    PR_TRUE /*isPerm*/, PR_TRUE /*isSensitive*/,
						    0/*pwdata*/);
  if (! privKey)
    {
      nsscommon_error (_("Unable to generate public/private key pair"));
      nssError ();
    }
  return privKey;
}

static CERTCertificateRequest *
generate_cert_request (SECKEYPublicKey *pubk, CERTName *subject)
{
  CERTSubjectPublicKeyInfo *spki = SECKEY_CreateSubjectPublicKeyInfo (pubk);
  if (! spki)
    {
      nsscommon_error (_("Unable to create subject public key info for certificate request"));
      nssError ();
      return 0;
    }
  
  /* Generate certificate request */
  CERTCertificateRequest *cr = CERT_CreateCertificateRequest (subject, spki, 0);
  SECKEY_DestroySubjectPublicKeyInfo (spki);
  if (! cr)
    {
      nsscommon_error (_("Unable to create certificate request"));
      nssError ();
    }
  return cr;
}

static CERTCertificate *
create_cert (CERTCertificateRequest *certReq, const string &dnsNames)
{
  // What is the current date and time?
  PRTime now = PR_Now ();

  // What is the date and time 1 year from now?
  PRExplodedTime printableTime;
  PR_ExplodeTime (now, PR_GMTParameters, & printableTime);
  printableTime.tm_month += 12;
  PRTime after = PR_ImplodeTime (& printableTime);
 
  // Note that the time is now in micro-second units.
  CERTValidity *validity = CERT_CreateValidity (now, after);
  if (! validity)
    {
      nsscommon_error (_("Unable to create certificate validity dates"));
      nssError ();
      return 0;
    }

  // Create a default serial number using the current time.
  PRTime serialNumber = now >> 19; // copied from certutil.

  // Create the certificate.
  CERTCertificate *cert = CERT_CreateCertificate (serialNumber, & certReq->subject, validity,
						  certReq);
  CERT_DestroyValidity (validity);
  if (! cert)
    {
      nsscommon_error (_("Unable to create certificate"));
      nssError ();
      return 0;
    }

  // Predeclare these to keep C++ happy about jumps to the label 'error'.
  SECStatus secStatus = SECSuccess;
  unsigned char keyUsage = 0x0;
  PRArenaPool *arena = 0;

  // Add the extensions that we need.
  void *extHandle = CERT_StartCertExtensions (cert);
  if (! extHandle)
    {
      nsscommon_error (_("Unable to allocate certificate extensions"));
      nssError ();
      goto error;
    }

  // Cert type extension.
  keyUsage |= (0x80 >> 1); // SSL Server
  keyUsage |= (0x80 >> 3); // Object signer
  keyUsage |= (0x80 >> 7); // Object signing CA

  SECItem bitStringValue;
  bitStringValue.data = & keyUsage;
  bitStringValue.len = 1;

  secStatus = CERT_EncodeAndAddBitStrExtension (extHandle,
						SEC_OID_NS_CERT_EXT_CERT_TYPE,
						& bitStringValue, PR_TRUE);
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_("Unable to encode certificate type extensions"));
      nssError ();
      goto error;
    }

  // Alternate dns name extension.
  if (! dnsNames.empty ())
    {
      arena = PORT_NewArena (DER_DEFAULT_CHUNKSIZE);
      if (! arena)
	{
	  nsscommon_error (_("Unable to allocate alternate DNS name extension for certificate"));
	  goto error;
	}

      // Walk down the comma separated list of names.
      CERTGeneralName *nameList = 0;
      CERTGeneralName *current = 0;
      PRCList *prev = 0;
      vector<string>components;
      tokenize (dnsNames, components, ",");
      for (unsigned i = 0; i < components.size (); ++i)
	{
	  char *tbuf = (char *)PORT_ArenaAlloc (arena, components[i].size () + 1);
	  strcpy (tbuf, components[i].c_str ());

	  current = (CERTGeneralName *)PORT_ZAlloc (sizeof (CERTGeneralName));
	  if (! current)
	    {
	      nsscommon_error (_("Unable to allocate alternate DNS name extension for certificate"));
	      goto error;
	    }
	  if (prev)
	    {
	      current->l.prev = prev;
	      prev->next = & current->l;
	    }
	  else
	    nameList = current;

	  current->type = certDNSName;
	  current->name.other.data = (unsigned char *)tbuf;
	  current->name.other.len = strlen (tbuf);
	  prev = & current->l;
	}

      // At this point nameList points to the head of a doubly linked,
      // but not yet circular, list and current points to its tail.
      if (nameList)
	{
	  // Make nameList circular.
	  nameList->l.prev = prev;
	  current->l.next = & nameList->l;

	  // Encode and add the extension.
	  SECItem item;
	  secStatus = CERT_EncodeAltNameExtension (arena, nameList, & item);
	  if (secStatus != SECSuccess)
	    {
	      nsscommon_error (_("Unable to encode alternate DNS name extension for certificate"));
	      nssError ();
	      goto error;
	    }
	  secStatus = CERT_AddExtension(extHandle,
					SEC_OID_X509_SUBJECT_ALT_NAME,
					& item, PR_FALSE, PR_TRUE);
	  if (secStatus != SECSuccess)
	    {
	      nsscommon_error (_("Unable to add alternate DNS name extension for certificate"));
	      nssError ();
	      goto error;
	    }
	}
    } // extra dns names specified.

  // We did not create any extensions on the cert request.
  assert (certReq->attributes != NULL);
  assert (certReq->attributes[0] == NULL);

  // Finished with cert extensions.
  secStatus = CERT_FinishExtensions (extHandle);
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_("Unable to complete alternate DNS name extension for certificate"));
      nssError ();
      goto error;
    }

  return cert;

 error:
  if (arena)
    PORT_FreeArena (arena, PR_FALSE);
  CERT_DestroyCertificate (cert);
  return 0;
}

static SECItem *
sign_cert (CERTCertificate *cert, SECKEYPrivateKey *privKey)
{
  SECOidTag algID = SEC_GetSignatureAlgorithmOidTag (privKey->keyType,
						     SEC_OID_UNKNOWN);
  if (algID == SEC_OID_UNKNOWN)
    {
      nsscommon_error (_("Unable to determine the signature algorithm for the signing the certificate"));
      nssError ();
      return 0;
    }

  PRArenaPool *arena = cert->arena;
  SECStatus rv = SECOID_SetAlgorithmID (arena, & cert->signature, algID, 0);
  if (rv != SECSuccess)
    {
      nsscommon_error (_("Unable to set the signature algorithm for signing the certificate"));
      nssError ();
      return 0;
    }

  /* we only deal with cert v3 here */
  *(cert->version.data) = 2;
  cert->version.len = 1;

  SECItem der;
  der.len = 0;
  der.data = 0;
  void *dummy = SEC_ASN1EncodeItem (arena, & der, cert,
				    SEC_ASN1_GET (CERT_CertificateTemplate));
  if (! dummy)
    {
      nsscommon_error (_("Unable to encode the certificate for signing"));
      nssError ();
      return 0;
    }

  SECItem *result = (SECItem *)PORT_ArenaZAlloc (arena, sizeof (SECItem));
  if (! result)
    {
      nsscommon_error (_("Unable to allocate memory for signing the certificate"));
      return 0;
    }

  rv = SEC_DerSignData (arena, result, der.data, der.len, privKey, algID);
  if (rv != SECSuccess)
    {
      nsscommon_error (_("Unable to sign the certificate"));
      nssError ();
      return 0;
    }

  cert->derCert = *result;
  return result;
}

static SECStatus
add_server_cert (const string &db_path, SECItem *certDER, PK11SlotInfo *slot)
{
  // Decode the cert.
  CERTCertificate *cert = CERT_NewTempCertificate(CERT_GetDefaultCertDB (), certDER,
      (char*)server_cert_nickname () /* nickname */,
      PR_FALSE /* isPerm */,
      PR_TRUE /* copyDER */);

  if (! cert)
    {
      nsscommon_error (_("Unable to decode certificate"));
      nssError ();
      return SECFailure;
    }

  // Import it into the database.
  CERTCertDBHandle *handle = 0;
  CERTCertTrust *trust = NULL;
  SECStatus secStatus = PK11_ImportCert (slot, cert, CK_INVALID_HANDLE,
					 server_cert_nickname (), PR_FALSE);
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_F("Unable to import certificate into the database at %s", db_path.c_str ()));
      nssError ();
      goto done;
    }
  
  // Make it a trusted server and signer.
  trust = (CERTCertTrust *)PORT_ZAlloc (sizeof (CERTCertTrust));
  if (! trust)
    {
      nsscommon_error (_("Unable to allocate certificate trust"));
      secStatus = SECFailure;
      goto done;
    }

  secStatus = CERT_DecodeTrustString (trust, "PCu,,PCu");
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_("Unable decode trust string 'PCu,,PCu'"));
      nssError ();
      goto done;
    }
    
  handle = CERT_GetDefaultCertDB ();
  assert (handle);
  secStatus = CERT_ChangeCertTrust (handle, cert, trust);
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_("Unable to change certificate trust"));
      nssError ();
    }

done:
  CERT_DestroyCertificate (cert);
  if (trust)
    PORT_Free (trust);
  return secStatus;
}

SECStatus
add_client_cert (const string &inFileName, const string &db_path, db_init_types db_init_type)
{
  FILE *inFile = fopen (inFileName.c_str (), "rb");
  if (! inFile)
    {
      nsscommon_error (_F("Could not open certificate file %s for reading\n%s",
			  inFileName.c_str (), strerror (errno)));
      return SECFailure;
    }

  int fd = fileno (inFile);
  struct stat info;
  int rc = fstat (fd, &info);
  if (rc != 0)
    {
      nsscommon_error (_F("Could not obtain information about certificate file %s\n%s",
			  inFileName.c_str (), strerror (errno)));
      fclose (inFile);
      return SECFailure;
    }

  SECItem certDER;
  certDER.len = info.st_size;
  certDER.data = (unsigned char *)PORT_Alloc (certDER.len);
  if (certDER.data == NULL)
    {
      nsscommon_error (_F("Could not allocate certDER\n%s",
			  strerror (errno)));
      fclose (inFile);
      return SECFailure;
    }
  size_t read = fread (certDER.data, 1, certDER.len, inFile);
  fclose (inFile);
  if (read != certDER.len)
    {
      nsscommon_error (_F("Error reading from certificate file %s\n%s",
			  inFileName.c_str (), strerror (errno)));
      return SECFailure;
    }

  NSSInitContext *context = NULL;
  if (db_init_type != db_no_nssinit)
    {
      // The http client adds a cert by calling us via add_server_trust which
      // first has already called nssInit; we don't want to call nssInit twice
      // See if the database already exists and can be initialized.
      SECStatus secStatus = SECFailure;
      // Make sure certificate database directory exists.
      if (create_client_cert_db (db_path.c_str ()) != 0)
        {
          nsscommon_error (_F("Could not make sure the certificate database directory %s exists",
              db_path.c_str ()));
          return SECFailure;
        }
      if (db_init_type == db_nssinitcontext)
        {
          context= nssInitContext (db_path.c_str (), 1/*readwrite*/, 0/*issueMessage*/);
          if (context == NULL)
            secStatus = SECFailure;
        }
      else
        secStatus = nssInit (db_path.c_str (), 1/*readwrite*/);
      if (secStatus != SECSuccess)
        {
          // Try again with a fresh database.
          if (clean_cert_db (db_path.c_str ()) != 0)
            {
              // Message already issued.
              return SECFailure;
            }

          // Make sure the given path exists.
          if (create_client_cert_db (db_path.c_str ()) != 0)
            {
              nsscommon_error (_F("Could not create certificate database directory %s",
                  db_path.c_str ()));
              return SECFailure;
            }

          // Initialize the new database.
          if (db_init_type == db_nssinitcontext)
            {
              context= nssInitContext (db_path.c_str (), 1/*readwrite*/);
              if (context == NULL)
                secStatus = SECFailure;
            }
          else
              secStatus = nssInit (db_path.c_str (), 1/*readwrite*/);
          if (secStatus != SECSuccess)
            return SECFailure;
        }
    }

  // Predeclare these to keep C++ happy about jumps to the label 'done'.
  CERTCertificate *cert = 0;
  CERTCertDBHandle *handle = 0;
  CERTCertTrust *trust = 0;
  PK11SlotInfo *slot = 0;
  SECStatus secStatus;

  // Add the cert to the database
  // Decode the cert.
  secStatus = SECFailure;
  cert = CERT_NewTempCertificate(CERT_GetDefaultCertDB (), &certDER,
      (char*)server_cert_nickname () /* nickname */,
      PR_FALSE /* isPerm */,
      PR_TRUE /* copyDER */);

  if (! cert)
    {
      nsscommon_error (_("Unable to decode certificate"));
      nssError ();
      goto done;
    }

  // We need the internal slot for this database.
  slot = PK11_GetInternalKeySlot ();
  if (! slot)
    {
      nsscommon_error (_F("Could not obtain internal key slot for certificate database %s", db_path.c_str()));
      nssError ();
      goto done;
    }

  // Import it into the database.
  secStatus = PK11_ImportCert (slot, cert, CK_INVALID_HANDLE,
			       server_cert_nickname (), PR_FALSE);
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_F("Could not import certificate into the database at %s", db_path.c_str()));
      nssError ();
      goto done;
    }
  
  if (PK11_NeedUserInit(slot)) {
    PK11_InitPin(slot, (char*)NULL, "");
  }
  
  // Make it a trusted SSL peer.
  trust = (CERTCertTrust *)PORT_ZAlloc (sizeof (CERTCertTrust));
  if (! trust)
    {
      nsscommon_error (_("Could not allocate certificate trust"));
      goto done;
    }

  secStatus = CERT_DecodeTrustString (trust, "P,P,P");
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_("Unable decode trust string 'P,P,P'"));
      nssError ();
      goto done;
    }

  handle = CERT_GetDefaultCertDB ();
  assert (handle);
  secStatus = CERT_ChangeCertTrust (handle, cert, trust);
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_("Unable to change certificate trust"));
      nssError ();
    }

 done:
  // Free NSS/NSPR objects and shutdown NSS.
  if (slot)
    PK11_FreeSlot (slot);
  if (trust)
    PORT_Free (trust);
  if (cert)
    CERT_DestroyCertificate (cert);
  if (certDER.data)
    PORT_Free (certDER.data);
  if (db_init_type != db_no_nssinit)
    nssCleanup (db_path.c_str (), context);

  // Make sure that the cert database files are read/write by the owner and
  // readable by all.
  glob_t globbuf;
  string filespec = db_path + "/*";
  int r = glob (filespec.c_str (), 0, NULL, & globbuf);
  if (r == GLOB_NOSPACE || r == GLOB_ABORTED) {
    // Not fatal, just a warning
    nsscommon_error (_F("Could not search certificate database directory %s", db_path.c_str ()));
  }
  else if (r != GLOB_NOMATCH)
    {
      mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
      for (unsigned i = 0; i < globbuf.gl_pathc; ++i)
	{
	  // Not fatal, just a warning
	  if (chmod (globbuf.gl_pathv[i], mode) != 0)
	    nsscommon_error (_F("Could set file permissions for %s", globbuf.gl_pathv[i]));
	}
      globfree(&globbuf);
    }
  
  return secStatus;
}

int
gen_cert_db (const string &db_path, const string &extraDnsNames, bool use_password)
{
  // Log the generation of a new database.
  log (_F("Generating a new certificate database directory in %s",
	  db_path.c_str ()));

  // Start with a clean cert database.
  if (clean_cert_db (db_path.c_str ()) != 0)
    {
      // Message already issued.
      return 1;
    }

  // Make sure the given path exists.
  if (create_server_cert_db (db_path.c_str ()) != 0)
    {
      nsscommon_error (_F("Could not create certificate database directory %s",
			  db_path.c_str ()));
      return 1;
    }

  // Initialize the new database.
  SECStatus secStatus = nssInit (db_path.c_str (), 1/*readwrite*/);
  if (secStatus != SECSuccess)
    {
      // Message already issued.
      return 1;
    }

  // Pre declare these to keep g++ happy about jumps to the label 'error'.
  CERTName *subject = 0;
  SECKEYPublicKey *pubkey = 0;
  SECKEYPrivateKey *privkey = 0;
  CERTCertificateRequest *cr = 0;
  CERTCertificate *cert = 0;
  SECItem *certDER = 0;
  string dnsNames;
  string hostname;
  int rc;
  string outFileName;
  FILE *outFile = 0;
  secStatus = SECFailure;

  // We need the internal slot for this database.
  PK11SlotInfo *slot = PK11_GetInternalKeySlot ();
  if (! slot)
    {
      nsscommon_error (_F("Could not obtain internal key slot for certificate database %s", db_path.c_str()));
      nssError ();
      goto error;
    }

  // Establish the password (if any) for the new database.
  rc = init_password (slot, db_path, use_password);
  if (rc != 0)
    {
      // Messages already issued.
      goto error;
    }

  // Format the cert subject.
  subject = CERT_AsciiToName ((char *)"CN=Systemtap Compile Server, OU=Systemtap");
  if (! subject)
    {
      nsscommon_error (_("Unable to encode certificate common header"));
      nssError ();
      goto error;
    }

  // Next, generate the private key.
  privkey = generate_private_key (db_path, slot, & pubkey);
  if (! privkey)
    {
      // Message already issued.
      goto error;
    }

  // Next, generate a cert request.
  cr = generate_cert_request (pubkey, subject);
  if (! cr)
    {
      // Message already issued.
      goto error;
    }

  // For the cert, we need our host name.
  struct utsname utsname;
  uname (& utsname);
  dnsNames = utsname.nodename;

  // Because avahi identifies hosts using a ".local" domain, add one to the list of names.
  hostname = dnsNames.substr (0, dnsNames.find ('.'));
  dnsNames += string(",") + hostname + ".local";

  // Add any extra names that were supplied.
  if (! extraDnsNames.empty ())
    dnsNames += "," + extraDnsNames;

  // Now, generate the cert.
  cert = create_cert (cr, dnsNames);
  CERT_DestroyCertificateRequest (cr);
  if (! cert)
    {
      // NSS error already issued.
      nsscommon_error (_("Unable to create certificate"));
      goto error;
    }

  // Sign the cert.
  certDER = sign_cert (cert, privkey);
  if (! certDER)
    {
      // Message already issued.
      goto error;
    }

  // Now output it to a file.
  outFileName = db_path + "/stap.cert";
  outFile = fopen (outFileName.c_str (), "wb");
  if (outFile)
    {
      size_t written = fwrite (certDER->data, 1, certDER->len, outFile);
      if (written != certDER->len)
	{
	  nsscommon_error (_F("Error writing to certificate file %s\n%s",
			      outFileName.c_str (), strerror (errno)));
	}
      fclose (outFile);
    }
  else
    {
      nsscommon_error (_F("Could not open certificate file %s for writing\n%s",
			  outFileName.c_str (), strerror (errno)));
    }

  // Add the cert to the database
  secStatus = add_server_cert (db_path, certDER, slot);
  CERT_DestroyCertificate (cert);
  if (secStatus != SECSuccess)
    {
      // NSS error already issued.
      nsscommon_error (_F("Unable to add certificate to %s", db_path.c_str ()));
      goto error;
    }

  // Done with the certificate database
  PK11_FreeSlot (slot);
  CERT_DestroyName (subject);
  SECKEY_DestroyPublicKey (pubkey);
  SECKEY_DestroyPrivateKey (privkey);
  goto done;

 error:
  if (slot)
    PK11_FreeSlot (slot);
  if (subject)
    CERT_DestroyName (subject);
  if (pubkey)
    SECKEY_DestroyPublicKey (pubkey);
  if (privkey)
    SECKEY_DestroyPrivateKey (privkey);
  if (cert)
    CERT_DestroyCertificate (cert); // Also destroys certDER.

 done:
  nssCleanup (db_path.c_str (), NULL);
  return secStatus != SECSuccess;
}

CERTCertList *get_cert_list_from_db (const string &cert_nickname)
{
  // Search the client-side database of trusted servers.
  CERTCertDBHandle *handle = CERT_GetDefaultCertDB ();
  assert (handle);
  CERTCertificate *db_cert = PK11_FindCertFromNickname (cert_nickname.c_str (), 0);
  if (! db_cert)
    {
      // No trusted servers. Not an error. Just an empty list returned.
      return 0;
    }

  // Here, we have one cert with the desired nickname.
  // Now, we will attempt to get a list of ALL certs 
  // with the same subject name as the cert we have.  That list 
  // should contain, at a minimum, the one cert we have already found.
  // If the list of certs is empty (0), the libraries have failed.
  CERTCertList *certs = CERT_CreateSubjectCertList (0, handle, & db_cert->derSubject,
						    PR_Now (), PR_FALSE);
  CERT_DestroyCertificate (db_cert);
  if (! certs)
    {
      nsscommon_error (_("NSS library failure in CERT_CreateSubjectCertList"));
      nssError ();
    }

  return certs;
}

static int
format_cert_validity_time (SECItem &vTime, char *timeString, size_t ts_size)
{
  int64 time;
  SECStatus secStatus;

  switch (vTime.type) {
  case siUTCTime:
    secStatus = DER_UTCTimeToTime (& time, & vTime);
    break;
  case siGeneralizedTime:
    secStatus = DER_GeneralizedTimeToTime (& time, & vTime);
    break;
  default:
    nsscommon_error (_("Could not decode certificate validity"));
    return 1;
  }
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_("Could not decode certificate validity time"));
      return 1;
    }

  // Convert to local time.
  PRExplodedTime printableTime;
  PR_ExplodeTime (time, PR_GMTParameters, & printableTime);
  if (! PR_FormatTime (timeString, ts_size, "%a %b %d %H:%M:%S %Y", & printableTime))
    {
      nsscommon_error (_("Could not format certificate validity time"));
      return 1;
    }

  return 0;
}

static bool
cert_is_valid (CERTCertificate *cert)
{
  // Verify the the certificate is valid as an SSL server and as an object signer and that
  // it is valid now.
  CERTCertDBHandle *handle = CERT_GetDefaultCertDB ();
  assert (handle);
  SECCertificateUsage usage = certificateUsageSSLServer | certificateUsageObjectSigner;
  SECStatus secStatus = CERT_VerifyCertificate (handle, cert, PR_TRUE/*checkSig*/, usage,
						PR_Now (), NULL, NULL/*log*/, & usage);
  return secStatus == SECSuccess;
}


bool
get_host_name (CERTCertificate *c, string &host_name)
{
  // Get the host name. It is in the alt-name extension of the
  // certificate.

  SECStatus secStatus;
  PRArenaPool *tmpArena = NULL;
  // A memory pool to work in
  tmpArena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
  if (! tmpArena)
    {
      clog << _("Out of memory:");
      return false;
    }

  SECItem subAltName;
  subAltName.data = NULL;
  secStatus = CERT_FindCertExtension (c,
				      SEC_OID_X509_SUBJECT_ALT_NAME,
				      & subAltName);
  if (secStatus != SECSuccess || ! subAltName.data)
    {
      clog << _("Unable to find alt name extension on server certificate: ") << endl;
      PORT_FreeArena (tmpArena, PR_FALSE);
      return false;
    }

  // Decode the extension.
  CERTGeneralName *nameList = CERT_DecodeAltNameExtension (tmpArena, & subAltName);
  SECITEM_FreeItem(& subAltName, PR_FALSE);
  if (! nameList)
    {
      clog << _("Unable to decode alt name extension on server certificate: ") << endl;
      return false;
    }

  // We're interested in the first alternate name.
  assert (nameList->type == certDNSName);
  host_name = string ((const char *)nameList->name.other.data,
		      nameList->name.other.len);
  PORT_FreeArena (tmpArena, PR_FALSE);
  return true;
}

static bool
cert_db_is_valid (const string &db_path, const string &nss_cert_name, 
		  CERTCertificate *this_cert)
{
  NSSInitContext *context;

  // Make sure the given path exists.
  if (! file_exists (db_path))
    {
      log (_F("Certificate database %s does not exist", db_path.c_str ()));
      return false;
    }

  // If a 'pw' file exists, then this is an old database. Treat any certs as invalid.
  if (file_exists (db_path + "/pw"))
    {
      log (_F("Certificate database %s is obsolete", db_path.c_str ()));
      return false;
    }

  // Initialize the NSS libraries -- readonly
  context = nssInitContext (db_path.c_str ());
  if (context == NULL)
    {
      // Message already issued.
      return false;
    }

  // Obtain a list of our certs from the database.
  bool valid_p = false;
  CERTCertList *certs = get_cert_list_from_db (nss_cert_name);
  if (! certs)
    {
      log (_F("No certificate found in database %s", db_path.c_str ()));
      goto done;
    }

  log (_F("Certificate found in database %s", db_path.c_str ()));
  for (CERTCertListNode *node = CERT_LIST_HEAD (certs);
       ! CERT_LIST_END (node, certs);
       node = CERT_LIST_NEXT (node))
    {
      // The certificate we're working with.
      CERTCertificate *c = node->cert;

      // Print the validity dates of the certificate.
      CERTValidity &v = c->validity;
      char timeString[256];
      if (format_cert_validity_time (v.notBefore, timeString, sizeof (timeString)) == 0)
	log (_F("  Not Valid Before: %s UTC", timeString));
      if (format_cert_validity_time (v.notAfter, timeString, sizeof (timeString)) == 0)
	log (_F("  Not Valid After: %s UTC", timeString));

      // Now ask NSS to check the validity.
      if (cert_is_valid (c))
	{
	  // The cert is valid. One valid cert is enough.
	  log (_("Certificate is valid"));
	  valid_p = true;
	  if (this_cert)
	    *this_cert = *c;
	  break;
	}

      // The cert is not valid. Look for another one.
      log (_("Certificate is not valid"));
    }
  CERT_DestroyCertList (certs);

 done:
  nssCleanup (db_path.c_str (), context);
  return valid_p;
}


#ifdef HAVE_HTTP_SUPPORT
/*
 * Similar to cert_db_is_valid; additionally it checks host_name and does no logging
 */
static bool
get_pem_cert_is_valid (const string &db_path, const string &nss_cert_name,
                       const string &host_name, CERTCertificate *this_cert)
{
  NSSInitContext *context;
  bool nss_already_init_p = false;
  bool found_match = false;

  if (! file_exists (db_path))
    return false;

  if (file_exists (db_path + "/pw"))
    return false;

  if (NSS_IsInitialized ())
    nss_already_init_p = true;

  context = nssInitContext (db_path.c_str (), 0 /* readWrite */, 0 /* issueMessage */);
  if (context == NULL)
    return false;

  CERTCertList *certs = get_cert_list_from_db (nss_cert_name);
  if (! certs)
    {
      nssCleanup (db_path.c_str (), context);
      return false;
    }

  for (CERTCertListNode *node = CERT_LIST_HEAD (certs);
       ! CERT_LIST_END (node, certs);
       node = CERT_LIST_NEXT (node))
    {
      CERTCertificate *c = node->cert;
      // An http client searches for the corresponding server's certificate
      if (host_name.length() > 0)
	{
	  string cert_host_name;
	  if (get_host_name (c, cert_host_name) == false)
	      continue;

	  if (cert_host_name != host_name)
	    {
	      size_t dot;
	      if ((dot = host_name.find ('.')) == string::npos
	          || cert_host_name != host_name.substr(0,dot))
	        continue;
	    }
	}

      // Now ask NSS to check the validity.
      if (cert_is_valid (c))
	{
	  if (this_cert)
	    *this_cert = *c;
	  found_match = true;
	  break;
	}
    }
  CERT_DestroyCertList (certs);

  // NSS shutdown is global and is forceful
  if (!nss_already_init_p)
    nssCleanup (db_path.c_str (), context);
  return found_match;
}


bool
cvt_nss_to_pem (CERTCertificate *c, string &cert_pem)
{
  BIO *bio;
  X509 *certificate_509;

  bio = BIO_new(BIO_s_mem());
  certificate_509 = X509_new();
  // Load the nss certificate into the openssl BIO
  int count = BIO_write (bio, (const void*)c->derCert.data, c->derCert.len);
  if (count == 0)
    return false;
  // Parse the BIO into an X509
  certificate_509 = d2i_X509_bio(bio, &certificate_509);
  // Convert the X509 to PEM form
  int rc = PEM_write_bio_X509(bio, certificate_509);
  if (rc != 1)
    return false;
  BUF_MEM *mem = NULL;
  // Load the buffer from the PEM form
  BIO_get_mem_ptr(bio, &mem);
  cert_pem = string(mem->data, mem->length);
  BIO_free(bio);
  X509_free(certificate_509);
  return true;
}


bool
get_pem_cert (const string &db_path, const string &nss_cert_name, const string &host,
    string &cert)
{
  CERTCertificate cc;
  CERTCertificate *c = &cc;
  // Do we have an nss certificate in the nss cert db for server host?
  if (get_pem_cert_is_valid (db_path, nss_cert_name, host, c))
    {
      // If we do, then convert to PEM form
      if (cvt_nss_to_pem (c, cert))
        return true;
    }
  return false;
}

bool
have_san_match (string & hostname, string & pem_cert)
{
  bool have_match = false;
  STACK_OF (GENERAL_NAME) * san_names = NULL;

  BIO *bio = BIO_new(BIO_s_mem());
  BIO_puts(bio, pem_cert.c_str());
  X509 *server_cert = PEM_read_bio_X509(bio, NULL, NULL, NULL);

  // Extract the source alternate names from the certificate
  san_names =
    (stack_st_GENERAL_NAME *) X509_get_ext_d2i ((X509 *) server_cert,
                                                NID_subject_alt_name, NULL,
                                                NULL);
  if (san_names == NULL)
    {
      return false;
    }

  for (int i = 0; i < sk_GENERAL_NAME_num (san_names); i++)
    {
      const GENERAL_NAME *current_name = sk_GENERAL_NAME_value (san_names, i);

      if (current_name->type == GEN_DNS)
        {
          const int scheme_len = 8; // https://
          string dns_name =
#if OPENSSL_VERSION_NUMBER<0x10100000L
              string ((char *) ASN1_STRING_data (current_name->d.dNSName));
#else
	      string ((char *) ASN1_STRING_get0_data (current_name->d.dNSName));
#endif

          if ((size_t) ASN1_STRING_length (current_name->d.dNSName) ==
              dns_name.length ())
            {
              if (hostname.compare(scheme_len,dns_name.length(), dns_name))
                {
                  have_match = true;
                  break;
                }
            }
        }
    }
  sk_GENERAL_NAME_pop_free (san_names, GENERAL_NAME_free);
  BIO_free(bio);
  X509_free(server_cert);

  return have_match;
}
#endif


// Ensure that our certificate exists and is valid. Generate a new one if not.
int
check_cert (const string &db_path, const string &nss_cert_name, bool use_db_password)
{
  // Generate a new cert database if the current one does not exist or is not valid.
  if (! cert_db_is_valid (db_path, nss_cert_name, NULL))
    {
      if (gen_cert_db (db_path, "", use_db_password) != 0)
	{
	  // NSS message already issued.
	  nsscommon_error (_("Unable to generate new certificate"));
	  return 1;
	}
    }
  return 0;
}

void sign_file (
  const string &db_path,
  const string &nss_cert_name,
  const string &inputName,
  const string &outputName
) {
  /* Get own certificate and private key. */
  CERTCertificate *cert = PK11_FindCertFromNickname (nss_cert_name.c_str (), NULL);
  if (cert == NULL)
    {
      nsscommon_error (_F("Unable to find certificate with nickname %s in %s.",
			  nss_cert_name.c_str (), db_path.c_str()));
      nssError ();
      return;
    }

  // Predeclare these to keep C++ happy abount branches to 'done'.
  unsigned char buffer[4096];
  PRFileDesc *local_file_fd = NULL;
  PRInt32 numBytes;
  SECStatus secStatus;
  SGNContext *sgn;
  SECItem signedData;

  /* db_path.c_str () gets passed to nssPasswordCallback */
  SECKEYPrivateKey *privKey = PK11_FindKeyByAnyCert (cert, (void *)db_path.c_str ());
  if (privKey == NULL)
    {
      nsscommon_error (_F("Unable to obtain private key from the certificate with nickname %s in %s.",
			  nss_cert_name.c_str (), db_path.c_str()));
      nssError ();
      goto done;
    }

  /* Sign the file. */
  /* Set up the signing context.  */
  sgn = SGN_NewContext (SEC_OID_PKCS1_SHA1_WITH_RSA_ENCRYPTION, privKey);
  if (! sgn) 
    {
      nsscommon_error (_("Could not create signing context"));
      nssError ();
      return;
    }
  secStatus = SGN_Begin (sgn);
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_("Could not initialize signing context."));
      nssError ();
      return;
    }

  /* Now read the data and add it to the signature.  */
  local_file_fd = PR_Open (inputName.c_str(), PR_RDONLY, 0);
  if (local_file_fd == NULL)
    {
      nsscommon_error (_F("Could not open module file %s", inputName.c_str ()));
      nssError ();
      return;
    }

  for (;;)
    {
      // No need for PR_Read_Complete here, since we're already managing multiple
      // reads to a fixed size buffer.
      numBytes = PR_Read (local_file_fd, buffer, sizeof (buffer));
      if (numBytes == 0)
	break;	/* EOF */

      if (numBytes < 0)
	{
	  nsscommon_error (_F("Error reading module file %s", inputName.c_str ()));
	  nssError ();
	  goto done;
	}

      /* Add the data to the signature.  */
      secStatus = SGN_Update (sgn, buffer, numBytes);
      if (secStatus != SECSuccess)
	{
	  nsscommon_error (_F("Error while signing module file %s", inputName.c_str ()));
	  nssError ();
	  goto done;
	}
    }

  /* Complete the signature.  */
  secStatus = SGN_End (sgn, & signedData);
  if (secStatus != SECSuccess)
    {
      nsscommon_error (_F("Could not complete signature of module file %s", inputName.c_str ()));
      nssError ();
      goto done;
    }

  SGN_DestroyContext (sgn, PR_TRUE);

  /* Now write the signed data to the output file.  */
  if(local_file_fd != NULL)
    PR_Close (local_file_fd);
  local_file_fd = PR_Open (outputName.c_str(), PR_WRONLY | PR_CREATE_FILE | PR_TRUNCATE,
			   PR_IRUSR | PR_IWUSR | PR_IRGRP | PR_IWGRP | PR_IROTH);
  if (local_file_fd == NULL)
    {
      nsscommon_error (_F("Could not open signature file %s", outputName.c_str ()));
      nssError ();
      goto done;
    }

  numBytes = PR_Write (local_file_fd, signedData.data, signedData.len);
  if (numBytes < 0 || numBytes != (PRInt32)signedData.len)
    {
      nsscommon_error (_F("Error writing to signature file %s", outputName.c_str ()));
      nssError ();
    }

 done:
  if (privKey)
    SECKEY_DestroyPrivateKey (privKey);
  CERT_DestroyCertificate (cert);
  if(local_file_fd != NULL)
    PR_Close (local_file_fd);
}

// PR_Read() is not guaranteed to read all of the requested data in one call.
// Iterate until all of the requested data has been read.
// Return the same values as PR_Read() would.
PRInt32 PR_Read_Complete (PRFileDesc *fd, void *buf, PRInt32 requestedBytes)
{
  // Read until EOF or until the expected number of bytes has been read.
  // PR_Read wants (void*), but we need (char *) to do address arithmetic.
  char *buffer = (char *)buf;
  PRInt32 totalBytes;
  PRInt32 bytesRead;
  for (totalBytes = 0; totalBytes < requestedBytes; totalBytes += bytesRead)
    {
      // Now read the data.
      bytesRead = PR_Read (fd, (void *)(buffer + totalBytes), requestedBytes - totalBytes);
      if (bytesRead == 0)
	break;	// EOF
      if (bytesRead < 0)
	return bytesRead; // Error
    }

  // Return the number of bytes we managed to read.
  return totalBytes;
}

SECStatus
read_cert_info_from_file (const string &certPath, string &fingerprint)
{
  FILE *certFile = fopen (certPath.c_str (), "rb");
  SECStatus secStatus = SECFailure;

  if (! certFile)
    {
      nsscommon_error (_F("Could not open certificate file %s for reading\n%s",
			  certPath.c_str (), strerror (errno)));
      return SECFailure;
    }

  int fd = fileno (certFile);
  struct stat info;
  int rc = fstat (fd, &info);
  if (rc != 0)
    {
      nsscommon_error (_F("Could not obtain information about certificate file %s\n%s",
			  certPath.c_str (), strerror (errno)));
      fclose (certFile);
      return SECFailure;
    }

  PLArenaPool *arena = PORT_NewArena(DER_DEFAULT_CHUNKSIZE);
  if (!arena)
    {
      nsscommon_error (_F("Could not create arena while decoding certificate from file %s",
			  certPath.c_str ()));
      fclose (certFile);
      goto done;
    }
      
  SECItem derCert;
  if (!SECITEM_AllocItem(arena, &derCert, info.st_size))
    {
      nsscommon_error (_F("Could not allocate DER cert\n%s",
			  strerror (errno)));
      fclose (certFile);
      goto done;
    }

  size_t read;
  read = fread (derCert.data, 1, derCert.len, certFile);
  fclose (certFile);
  if (read != derCert.len)
    {
      nsscommon_error (_F("Error reading from certificate file %s\n%s",
			  certPath.c_str (), strerror (errno)));
      goto done;
    }
  derCert.type = siDERCertBuffer;

  // Sigh. We'd like to use CERT_DecodeDERCertificate() here, but
  // although /usr/include/nss3/cert.h declares them, the shared
  // library doesn't export them.

  CERTCertificate *cert;
  int rv;
  char *str;

  // Strip off the signature.
  CERTSignedData *sd;
  sd = PORT_ArenaZNew(arena, CERTSignedData);
  if (!sd)
    {
      nsscommon_error (_F("Could not allocate signed data while decoding certificate from file %s",
			  certPath.c_str ()));
      goto done;
    }
  rv = SEC_ASN1DecodeItem(arena, sd, SEC_ASN1_GET(CERT_SignedDataTemplate), 
			  &derCert);
  if (rv)
    {
      nsscommon_error (_F("Could not decode signature while decoding certificate from file %s",
			  certPath.c_str ()));
      goto done;
    }

  // Decode the certificate.
  cert = PORT_ArenaZNew(arena, CERTCertificate);
  if (!cert)
    {
      nsscommon_error (_F("Could not allocate cert while decoding certificate from file %s",
			  certPath.c_str ()));
      goto done;
    }
  cert->arena = arena;
  rv = SEC_ASN1DecodeItem(arena, cert,
			  SEC_ASN1_GET(CERT_CertificateTemplate), &sd->data);
  if (rv)
    {
      nsscommon_error (_F("Could not decode certificate from file %s",
			  certPath.c_str ()));
      goto done;
    }

  // Get the fingerprint from the signature.
  unsigned char fingerprint_buf[SHA1_LENGTH];
  SECItem fpItem;
  rv = PK11_HashBuf(SEC_OID_SHA1, fingerprint_buf, derCert.data, derCert.len);
  if (rv)
    {
      nsscommon_error (_F("Could not decode SHA1 fingerprint from file %s",
			  certPath.c_str ()));
      goto done;
    }
  fpItem.data = fingerprint_buf;
  fpItem.len = sizeof(fingerprint_buf);
  str = CERT_Hexify(&fpItem, 1);
  if (! str)
  {
      nsscommon_error (_F("Could not hexify SHA1 fingerprint from file %s",
			  certPath.c_str ()));
      goto done;
  }
  fingerprint = str;
  transform(fingerprint.begin(), fingerprint.end(), fingerprint.begin(), 
	    ::tolower);
  PORT_Free(str);
  secStatus = SECSuccess;

done:
  if (arena)
    PORT_FreeArena(arena, PR_FALSE);
  return secStatus;
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */

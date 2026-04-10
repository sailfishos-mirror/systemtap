/*
  This program verifies the given file using the given signature, the named
  certificate and public key in the given certificate database.

  Copyright (C) 2009-2013, 2018 Red Hat Inc.

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

#include "../config.h"
#include "staprun.h"

#include <stdio.h>

#include <nspr.h>
#include <nss.h>
#include <pk11pub.h>
#include <cryptohi.h>
#include <cert.h>
#include <certt.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#include "../nsscommon.h"
#include "modverify.h"

// Called by some of the functions in nsscommon.cxx.
void
nsscommon_error (const char *msg, int logit __attribute ((unused)))
{
  fprintf (stderr, "%s\n", msg);
  fflush (stderr);
}


static int
verify_it (const char *signatureName, const SECItem *signature,
	   const char *module_name, const void *module_data, off_t module_size,
	   const SECKEYPublicKey *pubKey)
{
  VFYContext *vfy;
  SECStatus secStatus;
  int rc = MODULE_OK;

  /* Try SHA256 first (preferred), then SHA1 for backward compatibility.
     This restricts verification to only these two algorithms. */

  /* Create a verification context with SHA256.  */
  vfy = VFY_CreateContextDirect (pubKey, signature, SEC_OID_PKCS1_RSA_ENCRYPTION,
				 SEC_OID_SHA256, NULL, NULL);
  if (! vfy)
    {
      /* Try SHA1 for backward compatibility. */
      vfy = VFY_CreateContextDirect (pubKey, signature, SEC_OID_PKCS1_RSA_ENCRYPTION,
				     SEC_OID_SHA1, NULL, NULL);
      if (! vfy)
	{
	  /* The key does not match the signature, or unsupported algorithm.
	     This is not an error. It just means we are currently trying the
	     wrong certificate/key. i.e. the module remains untrusted for now.  */
	  rc = MODULE_UNTRUSTED;
	  goto done;
	}
    }

  /* Begin the verification process.  */
  secStatus = VFY_Begin(vfy);
  if (secStatus != SECSuccess)
    {
      fprintf (stderr, "Unable to initialize verification context while verifying %s using the signature in %s.\n",
	       module_name, signatureName);
      nssError ();
      rc = MODULE_CHECK_ERROR;
      goto done;
    }

  /* Add the data to be verified.  */
  secStatus = VFY_Update (vfy, module_data, module_size);
  if (secStatus != SECSuccess)
    {
      fprintf (stderr, "Error while verifying %s using the signature in %s.\n",
	       module_name, signatureName);
      nssError ();
      rc = MODULE_CHECK_ERROR;
      goto done;
    }

  /* Complete the verification.  */
  secStatus = VFY_End (vfy);
  if (secStatus != SECSuccess) {
    fprintf (stderr, "Unable to verify the signed module %s. It may have been altered since it was created.\n",
	     module_name);
    nssError ();
    rc = MODULE_ALTERED;
  }

 done:
  if (vfy)
    VFY_DestroyContext(vfy, PR_TRUE /*freeit*/);
  return rc;
}

int verify_module (const char *signatureName, const char* module_name,
		   const void *module_data, off_t module_size)
{
  const char *dbdir  = SYSCONFDIR "/systemtap/staprun";
  SECKEYPublicKey *pubKey;
  CERTCertList *certList;
  CERTCertListNode *certListNode;
  CERTCertificate *cert;
  PRStatus prStatus;
  PRFileInfo info;
  PRInt32  numBytes;
  PRFileDesc *local_file_fd;
  SECItem signature;
  int rc = 0;
  NSSInitContext *context;

  /* Call the NSPR initialization routines. */
  /* XXX: We shouldn't be using NSPR for a lot of this. */
  PR_Init (PR_SYSTEM_THREAD, PR_PRIORITY_NORMAL, 1);

  /* Get the size of the signature file.  */
  prStatus = PR_GetFileInfo (signatureName, &info);
  if (prStatus != PR_SUCCESS || info.type != PR_FILE_FILE || info.size < 0)
    {
      if (verbose>1) fprintf (stderr, "Signature file %s not found\n", signatureName);
      PR_Cleanup ();
      return MODULE_UNTRUSTED; /* Not signed */
    }

  /* Open the signature file.  */
  local_file_fd = PR_Open (signatureName, PR_RDONLY, 0);
  if (local_file_fd == NULL)
    {
      fprintf (stderr, "Could not open the signature file %s\n.", signatureName);
      nssError ();
      PR_Cleanup ();
      return MODULE_CHECK_ERROR;
    }

  /* Allocate space to read the signature file.  */
  signature.data = PORT_Alloc (info.size);
  if (! signature.data)
    {
      fprintf (stderr, "Unable to allocate memory for the signature in %s.\n", signatureName);
      nssError ();
      PR_Cleanup ();
      return MODULE_CHECK_ERROR;
    }

  numBytes = PR_Read_Complete (local_file_fd, signature.data, info.size);
  if (numBytes == 0) /* EOF */
    {
      fprintf (stderr, "EOF reading signature file %s.\n", signatureName);
      PR_Cleanup ();
      return MODULE_CHECK_ERROR;
    }
  if (numBytes < 0)
    {
      fprintf (stderr, "Error reading signature file %s.\n", signatureName);
      nssError ();
      PR_Cleanup ();
      return MODULE_CHECK_ERROR;
    }
  if (numBytes != info.size)
    {
      fprintf (stderr, "Incomplete data while reading signature file %s.\n", signatureName);
      PR_Cleanup ();
      return MODULE_CHECK_ERROR;
    }
  signature.len = info.size;

  /* Done with the signature file.  */
  PR_Close (local_file_fd);

  /* Initialize NSS. */
  context = nssInitContext (dbdir, 0/*readwrite*/, 1/*issueMessage*/, 1/*checkPermissions*/);
  if (context == NULL)
    {
      // Message already issued.
      return MODULE_CHECK_ERROR;
    }

  certList = PK11_ListCerts (PK11CertListAll, NULL);
  if (certList == NULL)
    {
      fprintf (stderr, "Unable to find certificates in the certificate database in %s.\n",
	       dbdir);
      nssError ();
      nssCleanup (dbdir, context);
      return MODULE_UNTRUSTED;
    }

  /* We need to look at each certificate in the database. */
  for (certListNode = CERT_LIST_HEAD (certList);
       ! CERT_LIST_END (certListNode, certList);
       certListNode = CERT_LIST_NEXT (certListNode))
    {
      cert = certListNode->cert;

      pubKey = CERT_ExtractPublicKey (cert);
      if (pubKey == NULL)
	{
	  fprintf (stderr, "Unable to extract public key from the certificate with nickname %s from the certificate database in %s.\n",
		   cert->nickname, dbdir);
	  nssError ();
	  rc = MODULE_CHECK_ERROR;
	  break;
	}

      /* Verify the file. */
      rc = verify_it (signatureName, & signature,
		      module_name, module_data, module_size, pubKey);
      if (rc == MODULE_OK || rc == MODULE_ALTERED || rc == MODULE_CHECK_ERROR)
	break; /* resolved or error */
    }

  CERT_DestroyCertList (certList);

  /* Shutdown NSS and exit NSPR gracefully. */
  nssCleanup (dbdir, context);
  PR_Cleanup ();

  return rc;
}

/* vim: set sw=2 ts=8 cino=>4,n-2,{2,^-2,t0,(0,u0,w1,M1 : */

#ifndef CRYPT_HELPERS_H
#define CRYPT_HELPERS_H

#include "tomcrypt.h"

class PRNGWrapper
{
  public:
	PRNGWrapper(const struct ltc_prng_descriptor* pPRNGDescriptor);
	~PRNGWrapper();

	int m_iPRNG;
	prng_state m_PRNG;
};

#endif

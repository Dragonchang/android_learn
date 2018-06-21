static unsigned short __enc (const char *str)
{
	int i;
	unsigned short ret = ((unsigned short) str [1] << 8) | (unsigned short) str [0];

	for (i = 0; i < (int) strlen (str); i ++)
	{
		if (i & 0x01)
		{
			ret += (((unsigned char) str [i] >> 1) + ((unsigned char) i << (i % 8)));
		}
		else
		{
			ret += (((unsigned char) str [i] << 1) - (((unsigned char) i * str [i + 1]) << 8));
		}
	}

	return ret;
}

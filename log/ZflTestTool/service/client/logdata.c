static int dump_logdata (int argc, char **argv)
{
	LOGDATA_HEADER hdr;
	LOGDATA data;
	int count;
	int fd = -1, ret = -1;

	if (argc != 3)
	{
		DM ("invalid arguments!\n");
		goto end;
	}

	if ((fd = open (argv [2], O_RDONLY)) < 0)
	{
		DM ("open %s: %s\n", argv [2], strerror (errno));
		goto end;
	}

	if ((count = read (fd, & hdr, sizeof (LOGDATA_HEADER))) < 0)
	{
		DM ("read header %s: %s\n", argv [2], strerror (errno));
		goto end;
	}

	if (count != sizeof (LOGDATA_HEADER))
	{
		DM ("read header %s: invalid header size %d!\n", argv [2], count);
		goto end;
	}

	DM ("magic       (%p) = [0x%08lX]\n",	& hdr.magic, hdr.magic);
	DM ("entry count (%p) = [%lu]\n",	& hdr.entry_count, hdr.entry_count);
	DM ("entry size  (%p) = [%lu]\n",	& hdr.entry_size, hdr.entry_size);
	DM ("index head  (%p) = [%lu]\n",	& hdr.index_head, hdr.index_head);
	DM ("index tail  (%p) = [%lu]\n",	& hdr.index_tail, hdr.index_tail);
	DM ("total size  (%p) = [%llu]\n",	& hdr.total_size, hdr.total_size);
	DM ("header size (%p) = [%lu (%lu)]\n", & hdr.header_size, hdr.header_size, (unsigned long) sizeof (LOGDATA_HEADER));

	if (hdr.header_size == 0)
	{
		/* hard-coded 40 because current data file does not have this field, MUST remove this in the future */
		DM ("(fix header size to 40 bytes)\n");
		hdr.header_size = 40;
	}

	if (hdr.entry_size != 0)
	{
		unsigned long index;
		int done;

		for (done = 0, index = hdr.index_tail;;)
		{
			lseek (fd, hdr.header_size + (index * sizeof (LOGDATA)), SEEK_SET);

			if ((count = read (fd, & data, sizeof (LOGDATA))) < 0)
			{
				DM ("read data #%lu: %s\n", index, strerror (errno));
				goto end;
			}

			if (count != sizeof (LOGDATA))
			{
				DM ("read data %lu: invalid data size %d!\n", index, count);
				goto end;
			}

			DM ("%4lu : [%12llu] [%s]\n", index, data.size, data.file);

			if (index == hdr.index_head)
				break;

			if (++ index >= hdr.entry_count)
			{
				index = 0;
			}
		}
	}

	ret = 0;
end:;
	if (fd >= 0) close (fd);
	return ret;
}

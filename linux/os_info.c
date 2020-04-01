/*------------------------------------------------------------------------
 * os_info.c
 *              Operating system information
 *
 * Copyright (c) 2020, EnterpriseDB Corporation. All Rights Reserved.
 *
 *------------------------------------------------------------------------
 */

#include "postgres.h"
#include "stats.h"

#include <unistd.h>
#include <sys/utsname.h>

#define USER_INFO_FILE "/etc/passwd"

bool total_opened_handle(int *total_handles)
{
	FILE          *fp;
	char          *line_buf = NULL;
	size_t        line_buf_size = 0;
	ssize_t       line_size;
	int           allocated_handle_count;
	int           unallocated_handle_count;
	int           max_handle_count;
	const char    *scan_fmt = "%d %d %d";

	fp = fopen(OS_HANDLE_READ_FILE_PATH, "r");

	if (!fp)
	{
		ereport(DEBUG1, (errmsg("can not open file for reading handle informations")));
		return false;
	}

	/* Get the first line of the file. */
	line_size = getline(&line_buf, &line_buf_size, fp);

	/* Loop through until we are done with the file. */
	if (line_size >= 0)
		sscanf(line_buf, scan_fmt, &allocated_handle_count, &unallocated_handle_count, &max_handle_count);

	if (line_buf != NULL)
	{
		free(line_buf);
		line_buf = NULL;
	}

	fclose(fp);

	*total_handles = allocated_handle_count;

	return true;
}

bool os_boot_up_since_seconds(float4 *boot_up_seconds)
{
	FILE          *fp;
	char          *line_buf = NULL;
	size_t        line_buf_size = 0;
	ssize_t       line_size;
	float4        os_boot_up_seconds;
	const char    *scan_fmt = "%f %*f";

	fp = fopen(OS_BOOT_UP_SINCE_FILE_PATH, "r");

	if (!fp)
	{
		ereport(DEBUG1, (errmsg("can not open file for reading os boot up informations")));
		return false;
	}

	/* Get the first line of the file. */
	line_size = getline(&line_buf, &line_buf_size, fp);

	/* Loop through until we are done with the file. */
	if (line_size >= 0)
		sscanf(line_buf, scan_fmt, &os_boot_up_seconds);

	if (line_buf != NULL)
	{
		free(line_buf);
		line_buf = NULL;
	}

	fclose(fp);

	*boot_up_seconds = os_boot_up_seconds;

	return true;
}

int get_total_users()
{
	FILE     *fp = NULL;
	int      number_of_users = 0;
	char     *line_buf = NULL;
	size_t   line_buf_size = 0;
	ssize_t  line_size;

	fp = fopen(USER_INFO_FILE, "r");

	if (fp == NULL)
	{
		ereport(DEBUG1, (errmsg("[get_total_users]: error while opening file")));
		return 0;
	}

	/* Get the first line of the file. */
	line_size = getline(&line_buf, &line_buf_size, fp);

	/* Loop through until we are done with the file. */
	while (line_size >= 0)
	{
		number_of_users++;

		/* Free the allocated line buffer */
		if (line_buf != NULL)
		{
			free(line_buf);
			line_buf = NULL;
		}

		/* Get the next line */
		line_size = getline(&line_buf, &line_buf_size, fp);
	}

	/* Free the allocated line buffer */
	if (line_buf != NULL)
	{
		free(line_buf);
		line_buf = NULL;
	}

	if (fp != NULL)
		fclose(fp);

	return number_of_users;
}

void ReadOSInformations(Tuplestorestate *tupstore, TupleDesc tupdesc)
{
	struct     utsname uts;
	Datum      values[Natts_os_info];
	bool       nulls[Natts_os_info];
	char       host_name[MAXPGPATH];
	char       domain_name[MAXPGPATH];
	char       version[MAXPGPATH];
	char       architecture[MAXPGPATH];
	char       os_name[MAXPGPATH];
	int        ret_val;
	FILE       *os_info_file;
	char       *line_buf = NULL;
	size_t     line_buf_size = 0;
	ssize_t    line_size;
	int        active_processes = 0;
	int        running_processes = 0;
	int        sleeping_processes = 0;
	int        stopped_processes = 0;
	int        zombie_processes = 0;
	int        total_threads = 0;
	int        handle_count = 0;
	float4     os_up_since_seconds = 0;

	memset(nulls, 0, sizeof(nulls));
	memset(host_name, 0, MAXPGPATH);
	memset(domain_name, 0, MAXPGPATH);
	memset(version, 0, MAXPGPATH);
	memset(architecture, 0, MAXPGPATH);
	memset(os_name, 0, MAXPGPATH);

	ret_val = uname(&uts);
	/* if it returns not zero means it fails so set null values */
	if (ret_val != 0)
	{
		nulls[Anum_os_version]  = true;
		nulls[Anum_architecture] = true;
	}
	else
	{
		sprintf(version, "%s %s", uts.sysname, uts.release);
		memcpy(architecture, uts.machine, strlen(uts.machine));
	}

	/* Get total number of OS users */
	int total_users = get_total_users();

	/* Function used to get the host name of the system */
	if (gethostname(host_name, sizeof(host_name)) != 0)
		ereport(DEBUG1,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("error while getting host name")));

	/* Function used to get the domain name of the system */
	if (getdomainname(domain_name, sizeof(domain_name)) != 0)
		ereport(DEBUG1,
				(errcode(ERRCODE_INVALID_PARAMETER_VALUE),
					errmsg("error while getting domain name")));

	/*If hostname or domain name is empty, set the value to NULL */
	if (strlen(host_name) == 0)
		nulls[Anum_host_name] = true;
	if (strlen(domain_name) == 0)
		nulls[Anum_domain_name] = true;

	os_info_file = fopen(OS_INFO_FILE_NAME, "r");

	if (!os_info_file)
	{
		char os_info_file_name[MAXPGPATH];
		snprintf(os_info_file_name, MAXPGPATH, "%s", OS_INFO_FILE_NAME);

		ereport(DEBUG1,
				(errcode_for_file_access(),
					errmsg("can not open file %s for reading os information",
						os_info_file_name)));

		nulls[Anum_os_name] = true;
	}
	else
	{
		/* Get the first line of the file. */
		line_size = getline(&line_buf, &line_buf_size, os_info_file);

		/* Loop through until we are done with the file. */
		while (line_size >= 0)
		{
			int len = strlen(line_buf);
			if (strstr(line_buf, OS_DESC_SEARCH_TEXT) != NULL)
				memcpy(os_name, (line_buf + strlen(OS_DESC_SEARCH_TEXT)), (len - strlen(OS_DESC_SEARCH_TEXT)));

			/* Free the allocated line buffer */
			if (line_buf != NULL)
			{
				free(line_buf);
				line_buf = NULL;
			}

			/* Get the next line */
			line_size = getline(&line_buf, &line_buf_size, os_info_file);
		}

		/* Free the allocated line buffer */
		if (line_buf != NULL)
		{
			free(line_buf);
			line_buf = NULL;
		}

		fclose(os_info_file);
	}

	/* Get total file descriptor, thread count and process count */
	if (read_process_status(&active_processes, &running_processes, &sleeping_processes,
							&stopped_processes, &zombie_processes, &total_threads))
	{
		values[Anum_os_process_count] = active_processes;
		values[Anum_os_thread_count] = total_threads;
	}
	else
	{
		nulls[Anum_os_process_count] = true;
		nulls[Anum_os_thread_count] = true;
	}

	/* licenced user is not applicable to linux so return NULL */
	nulls[Anum_number_of_licensed_users] = true;
	nulls[Anum_os_boot_time] = true;

	/* count the total number of opended file descriptor */
	if (total_opened_handle(&handle_count))
		values[Anum_os_handle_count] = handle_count;
	else
		nulls[Anum_os_handle_count] = handle_count;

	if (os_boot_up_since_seconds(&os_up_since_seconds))
		values[Anum_os_up_since_seconds] = os_up_since_seconds;
	else
		nulls[Anum_os_up_since_seconds] = true;

	values[Anum_os_name]             = CStringGetTextDatum(os_name);
	values[Anum_host_name]           = CStringGetTextDatum(host_name);
	values[Anum_domain_name]         = CStringGetTextDatum(domain_name);
	values[Anum_number_of_users]     = Int32GetDatum(total_users);
	values[Anum_os_version]          = CStringGetTextDatum(version);
	values[Anum_architecture]        = CStringGetTextDatum(architecture);
	values[Anum_os_up_since_seconds] = Float4GetDatum(os_up_since_seconds);

	tuplestore_putvalues(tupstore, tupdesc, values, nulls);
}

/*
 * p4_args.c   Code that looks at the arguments, recognizes any that are
 *             for p4, uses the arguments, and removes them from the
 *             command line args.
 */
#include "p4.h"
#include "p4_sys.h"
#include <sys/poll.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

/* Macro used to see if an arg is not following the correct format. */
#define bad_arg(a)    ( ((a)==NULL) || ((*(a)) == '-') )

static char pgm[100];		/* Used to keep argv[0] for the usage cmd. */

static P4VOID usage (void);
static P4VOID print_version_info (void);
static P4VOID strip_out_args (char **, int*, int *, int);


P4VOID process_args(int *argc, char **argv)
{
    int i,c,nextarg;
    FILE *fp;
    char *s, **a;
    struct p4_procgroup_entry *pe;

    if (!argc || !argv) {
	/* Failure if either argc or argv are null */
        p4_error( "Command-line arguments are missing",0 );
    }

    /* Put the name of the called program (according to the args) into pgm */
    s = (char *)  rindex(*argv, '/');
    if (s)
	strcpy(pgm, s + 1);
    else
	strcpy(pgm, *argv);

    /* Set all command line flags (except procgroup) to their defaults */
    p4_debug_level = 0;
    p4_remote_debug_level = 0;
    bm_outfile[0] = '\0';
    procgroup_file[0] = '\0';
    p4_wd[0] = '\0';
    strcpy(local_domain, "");
    p4_myname_in_procgroup[0] = '\0';
    hand_start_remotes = P4_FALSE;
    execer_starting_remotes = P4_FALSE;
    execer_id[0] = '\0';
    execer_masthost[0] = '\0';
#ifdef OLD_EXECER
    execer_jobname[0] = '\0';
#endif
    execer_mynodenum = 0;
    execer_mastport = 0;
    execer_pg = NULL;

    /*
     * For SLURM based job initiations (from srun command), get the
     * parameters from environment variables as needed. This allows
     * for a truly parallel job launch using the existing "execer"
     * mode of operation with slight modification.
     */
    if (getenv("SLURM_JOBID")) {
	int i;
	char *tmp, *hostlist, *host2, *tasks_per_node, *task2;

	execer_starting_remotes = P4_TRUE;
	strcpy(execer_id, "mpiexec");

	if ((tmp = getenv("SLURMD_NODENAME")))
	    strcpy(execer_myhost, tmp);
	else {
	    printf("SLURMD_NODENAME environment variable missing\n");
	    exit(-1);
	}

	if ((tmp = getenv("SLURM_NODEID")))
	    execer_mynodenum = atoi(tmp);
	else {
	    printf("SLURM_NODEID environment variable missing\n");
	    exit(-1);
	}

	if ((tmp = getenv("SLURM_NNODES")))
	    execer_numtotnodes = atoi(tmp);
	else {
	    printf("SLURM_NNODES environment variable missing\n");
	    exit(-1);
	}

	if (!(tmp = getenv("SLURM_MPICH_NODELIST"))) {
	    printf("SLURM_MPICH_NODELIST environment variable missing\n");
	    printf("  SLURM's mpich1_p4 plugin likely not in use\n");
	    exit(-1);
	}
	i = strlen(tmp) + 1;
	hostlist = malloc(i);
	bcopy(tmp, hostlist, i);
	tmp = strtok_r(hostlist, ",", &host2);
	if (!tmp) {
	    printf("SLURM_MPICH_NODELIST environment variable invalid\n");
	    exit(-1);
	}
	strcpy(execer_masthost, tmp);

	if (!(tmp = getenv("SLURM_MPICH_TASKS"))) {
	    printf("SLURM_MPICH_TASKS environment variable missing\n");
	    exit(-1);
	}
	i = strlen(tmp) + 1;
	tasks_per_node = malloc(i);
	bcopy(tmp, tasks_per_node, i);
	tmp = strtok_r(tasks_per_node, ",", &task2);
	if (!tmp) {
	    printf("SLURM_MPICH_TASKS environment variable invalid\n");
	    exit(-1);
	}
	execer_mynumprocs = atoi(tmp);

	if (execer_mynodenum == 0) {
	    if ((tmp = getenv("SLURM_MPICH_PORT1")))
		execer_mastport = atoi(tmp);
	    else {
		printf("SLURM_MPICH_PORT1 environment variable missing\n");
		exit(-1);
	    }	
	    execer_pg = p4_alloc_procgroup();
	    pe = execer_pg->entries;
	    strcpy(pe->host_name, execer_myhost);
	    pe->numslaves_in_group = execer_mynumprocs - 1;
	    strcpy(pe->slave_full_pathname, argv[0]);
	    pe->username[0] = '\0'; /* unused */
	    execer_pg->num_entries++;
	    for (i=0; i<(execer_numtotnodes-1); i++) {
		pe++;
		tmp = strtok_r(NULL, ",", &host2);
		if (!tmp) {
		    printf("SLURM_MPICH_NODELIST environment variable invalid\n");
		    exit(-1);
		}
		strcpy(pe->host_name, tmp);
		tmp = strtok_r(NULL, ",", &task2);
		if (!tmp) {
		    printf("SLURM_MPICH_TASKS environment variable invalid\n");
		    exit(-1);
		}
		pe->numslaves_in_group = atoi(tmp);
#if 0
		printf("host[%d] name:%s tasks:%d\n", 
			i, pe->host_name, pe->numslaves_in_group);
#endif
		*pe->slave_full_pathname = 0;
		pe->username[0] = '\0'; /* unused */
		execer_pg->num_entries++;
	    }
	} else {
	    int p4_fd2, cc;
	    short new_port;
	    struct sockaddr_in serv_addr;
	    socklen_t serv_len;
	    struct pollfd ufds;

	    if (strcmp(execer_myhost, execer_masthost)) {
		/* look up correct task count */
		for (i=0; i<execer_numtotnodes; i++) {
		    tmp = strtok_r(NULL, ",", &host2);
		    if (!tmp) break;
		    cc = strcmp(execer_myhost, tmp);
		    tmp = strtok_r(NULL, ",", &task2);
		    if (!tmp) break;
		    if (cc) continue;
		    execer_mynumprocs = atoi(tmp);
		    break;
		}
	    }

	    /* Read the correct port to use from srun */
	    if ((tmp = getenv("SLURM_MPICH_PORT2")))
		execer_mastport = atoi(tmp);
	    else {
		printf("SLURM_MPICH_PORT2 environment variable missing\n");
		exit(-1);
	    }
	    if ((p4_fd2 = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		exit(-1);
	    }
	    bzero((char *) &serv_addr, sizeof(serv_addr));
	    serv_addr.sin_family = PF_INET;
	    serv_addr.sin_port = htons(execer_mastport);
	    inet_pton(AF_INET, getenv("SLURM_LAUNCH_NODE_IPADDR"),
			(void *) &serv_addr.sin_addr);
	    serv_len = sizeof(serv_addr);
	    /* stagger the requests */
	    usleep(2000 + execer_mynodenum * 300);
	    if (connect(p4_fd2, (struct sockaddr *) &serv_addr, serv_len) < 0) {
		perror("connect");
		exit(-1);
	    }
	    ufds.fd = p4_fd2;
	    ufds.events = POLLOUT | POLLERR | POLLHUP;
	    ufds.revents = 0;
poll1:	    cc = poll(&ufds, 1, 10000);		/* wait 10 secs max */
	    if (cc == 0) {
		printf("p4 startup, poll1 timeout\n");
		exit(-1);
	    } else if (cc < 0) {
		if (errno == EINTR) goto poll1;
		perror("poll1");
		exit(-1);
	    } else if ((ufds.revents & POLLOUT) == 0) {
		printf("p4 startup, poll1 failure\n");
		exit(-1);
	    }
write1:	    cc = write(p4_fd2, "GET", 4);
	    if (cc < 0) {
		if (errno == EINTR) goto write1;
		perror("write");
		exit(-1);
	    } else if (cc < 4) {
		printf("p4 startup, partitial write\n");
		exit(-1);
	    }
	    ufds.fd = p4_fd2;
	    ufds.events = POLLIN | POLLERR | POLLHUP;
	    ufds.revents = 0;
poll2:	    cc = poll(&ufds, 1, 10000);		/* wait 10 secs max */
	    if (cc == 0) {
		printf("p4 startup, poll2 timeout\n");
		exit(-1);
	    } else if (cc < 0) {
		if (errno == EINTR) goto poll2;
		perror("poll2");
		exit(-1);
	    } else if ((ufds.revents & POLLIN) == 0) {
		printf("p4 startup, poll2 failure\n");
		exit(-1);
	    }
read2:	    cc = read(p4_fd2, &new_port, sizeof(new_port)); 
	    if (cc < 0) {
		if (errno == EINTR) goto read2;
		perror("read2");
		exit(-1);
	    } else if (cc != sizeof(new_port)) {
		printf("p4 startup, partitial read\n");
		exit(-1);
	    }
	    close(p4_fd2);
	    execer_mastport = new_port;
	}
	free(hostlist);
	free(tasks_per_node);
#if 0
	printf("execer_id:%s\n", execer_id);
	printf("execer_myhost:%s\n", execer_myhost);
	printf("execer_mynodenum:%d\n", execer_mynodenum);
	printf("execer_numtotnodes:%d\n", execer_numtotnodes);
	printf("execer_mastport:%d\n", execer_mastport);
	printf("execer_masthost:%s\n", execer_masthost);
	printf("execer_mynumprocs:%d\n", execer_mynumprocs);
#endif
    }

    /* Move to last argument, so that we can go backwards. */
    a = &argv[*argc - 1];

    /*
     * Loop backwards through arguments, catching the ones that start with
     * '-'.  Backwards is more efficient when you are stripping things out.
     */
    for (c = (*argc); c > 1; c--, a--)
    {
	if (**a != '-')
	    continue;

        if (strcmp(*a, "-execer_id") == 0)
        {
	    /*
	     * Format of the rest of the args, example job:
	     *   node00:1 + node01:3 + node02:1
	     * Big master:
	     * a.out -execer_id mpiexec -master_host node00 -my_hostname node00
	     *   -my_nodenum 0 -my_numprocs 1 -total_numnodes 3 -mastport 4444
	     *  -remote_info node01 3 node02 1
	     * Remote masters:
	     * a.out -execer_id mpiexec -master_host node00 -my_hostname node01
	     *  -my_nodenum 1 -my_numprocs 3 -total_numnodes 3 -master_port 5555
	     * a.out -execer_id mpiexec -master_host node00 -my_hostname node02
	     *  -my_nodenum 2 -my_numprocs 1 -total_numnodes 3 -master_port 5555
	     *
	     * Master will be started first, then report its listening
	     * socket, then slaves can be started all at once in any order.
	     */
            execer_starting_remotes = P4_TRUE;
            strcpy(execer_id,*(a+1));
            strcpy(execer_masthost,*(a+3));
            strcpy(execer_myhost,*(a+5));
            execer_mynodenum = atoi(*(a+7));
            execer_mynumprocs = atoi(*(a+9));
	    execer_numtotnodes = atoi(*(a+11));
#ifdef OLD_EXECER
            strcpy(execer_jobname,*(a+13));
#else
	    execer_mastport = atoi(*(a+13));
	    nextarg = 14;
#endif
	    if (execer_mynodenum == 0)
	    {
		execer_pg = p4_alloc_procgroup();
		pe = execer_pg->entries;
		strcpy(pe->host_name,execer_myhost);
		pe->numslaves_in_group = execer_mynumprocs - 1;
		strcpy(pe->slave_full_pathname,argv[0]);
		pe->username[0] = '\0'; /* unused */
		execer_pg->num_entries++;
		for (i=0; i < (execer_numtotnodes-1); i++)
		{
		    if (i == 0)
			++nextarg;  /* "-remote_info" fake arg */
		    pe++;
		    strcpy(pe->host_name,*(a+nextarg));
		    nextarg++;
#ifdef OLD_EXECER
		    nextarg++;  /* skip node num */
#endif
		    pe->numslaves_in_group = atoi(*(a+nextarg));
		    nextarg++;
#ifdef OLD_EXECER
		    strcpy(pe->slave_full_pathname,*(a+nextarg)); /* unused */
		    nextarg++;
#else
		    *pe->slave_full_pathname = 0;
#endif
		    pe->username[0] = '\0'; /* unused */

		    execer_pg->num_entries++;
		}
	    }
#ifdef OLD_EXECER
	    else
	    {
		execer_mastport = get_execer_port(execer_masthost);
	    }
#else
	    strip_out_args(a, argc, &c, nextarg);
#endif
            continue;
        }

	if (!strcmp(*a, "-p4pg"))
	{
	    if (bad_arg(a[1]))
		usage();
	    strncpy(procgroup_file, a[1], 256);
	    procgroup_file[255] = 0;
	    strip_out_args(a, argc, &c, 2);
	    continue;
	}
	if (!strcmp(*a, "-p4wd"))
	{
	    if (bad_arg(a[1]))
		usage();
	    strncpy(p4_wd, a[1], 255);
	    p4_wd[255] = 0;
	    strip_out_args(a, argc, &c, 2);
	    continue;
	}
	if (!strcmp(*a, "-p4dbg"))
	{
	    if (bad_arg(a[1]))
		usage();
	    p4_debug_level = atoi(a[1]);
	    strip_out_args(a, argc, &c, 2);
	    continue;
	}
	if (!strcmp(*a, "-p4ssport"))
	{
	    if (bad_arg(a[1]))
		usage();
	    sserver_port = atoi(a[1]);
	    strip_out_args(a, argc, &c, 2);
	    continue;
	}
	if (!strcmp(*a, "-p4rdbg"))
	{
	    if (bad_arg(a[1]))
		usage();
	    p4_remote_debug_level = atoi(a[1]);
	    strip_out_args(a, argc, &c, 2);
	    continue;
	}
	if (!strcmp(*a, "-p4gm"))
	{
	    if (bad_arg(a[1]))
		usage();
	    globmemsize = atoi(a[1]);
	    strip_out_args(a, argc, &c, 2);
	    continue;
	}
	if (!strcmp(*a, "-p4dmn"))
	{
	    if (bad_arg(a[1]))
		usage();
	    strcpy(local_domain, a[1]);
	    strip_out_args(a, argc, &c, 2);
	    continue;
	}
	if (!strcmp(*a, "-p4out"))
	{
	    if (bad_arg(a[1]))
		usage();
	    strncpy(bm_outfile, a[1], 100);
	    bm_outfile[99] = 0;
	    strip_out_args(a, argc, &c, 2);
	    continue;
	}
	if (!strcmp(*a, "-p4rout"))
	{
	    if (bad_arg(a[1]))
		usage();
	    strncpy(rm_outfile_head, a[1], 100);
	    rm_outfile_head[99] = 0;
	    strip_out_args(a, argc, &c, 2);
	    continue;
	}
	if (!strcmp(*a, "-p4log"))
	{
	    strip_out_args(a, argc, &c, 1);
	    logging_flag = P4_TRUE;
	    continue;
	}
	if (!strcmp(*a, "-p4norem"))
	{
	    strip_out_args(a, argc, &c, 1);
	    hand_start_remotes = P4_TRUE;
	    continue;
	}
	if (!strcmp(*a, "-p4version"))
	{
	    strip_out_args(a, argc, &c, 1);
	    print_version_info();
	    continue;
	}
	/* Add escape for socket performance controls */
	if (!strcmp( *a, "-p4sctrl" ))
	{
	    if (bad_arg(a[1]))
		usage();
	    p4_socket_control( a[1] );
	    strip_out_args(a, argc, &c, 2);
	    continue;
	}

	if (!strcmp(*a, "-p4yourname"))
	{
	    /* Capture the name that the master is using in its procgroup
	       file.  This really belongs with the various "remote master"
	       arguments, but putting it there will mess up lots of 
	       code.  Using a separate argument for this makes it
	       easier to make this an optional value */
	    if (bad_arg(a[1]))
		usage();
	    strncpy(p4_myname_in_procgroup, a[1], MAXHOSTNAMELEN);
	    strip_out_args(a, argc, &c, 2);
	    continue;
	}
	if (!strcmp( *a, "-p4rmrank")) {
	    /* Capture the base rank for this remote master.  That is,
	       the rank of the remote master.  */
	    if (bad_arg(a[1]))
		usage();
	    p4_rm_rank = atoi(a[1]);
	    strip_out_args(a, argc, &c, 2);
	    continue;
	}
	if (!strcmp(*a, "-p4help"))
	    usage();
    }
    if (!execer_starting_remotes) {
	if (procgroup_file[0] == '\0')
	{
	    strncpy(procgroup_file,argv[0],250);
	    procgroup_file[249] = 0;
	    strcat(procgroup_file,".pg");
	    if ((fp = fopen(procgroup_file,"r")) == NULL) {
                /* pgm.pg not there */
		strcpy(procgroup_file, "procgroup");
	    }
	    else
		fclose(fp);
	}
	p4_dprintfl(10,"using procgroup file %s\n",procgroup_file);
    }
}

static P4VOID strip_out_args(char **argv, int *argc, int *c, int num)
{
    char **a;
    int i;

    /* Strip out the argument. */
    (*argc) -= num;
    for (a = argv, i = (*c); i <= *argc; i++, a++)
	*a = (*(a + num));
    *a = 0;   /* make last null just in case */
}

static P4VOID usage( void )
{
    print_version_info();
    printf("p4 usage: %s [p4 options]\n", pgm);
    printf("Valid p4 options:\n");
    printf("\t-p4help            get this message\n");
    printf("\t-p4pg      <file>  set procgroup file\n");
    printf("\t-p4dbg    <level>  set debug level\n");
    printf("\t-p4rdbg   <level>  set remote debug level\n");
    printf("\t-p4gm      <size>  set globmemsize\n");
    printf("\t-p4dmn   <domain>  set domainname\n");
    printf("\t-p4out     <file>  set output file for master\n");
    printf("\t-p4rout    <file>  set output file prefix for remote masters\n");
    printf("\t-p4ssport <port#>  set private port number for secure server\n");
    printf("\t-p4norem           don't start remote processes\n");
    printf("\t-p4sctrl <string>  set socket control features\n");
#ifdef ALOG_TRACE
    printf("\t-p4log             enable internal p4 logging by alog\n");
#endif
    printf("\t-p4version         print current p4 version number\n");
    printf("\n");
    exit(-1);

}

static P4VOID print_version_info( void )
{
        printf("\n");
        printf("p4  version number: %s\n",P4_PATCHLEVEL);
        printf("p4 date configured: %s\n",P4_CONFIGURED_TIME);
        printf("p4    machine type: %s\n",P4_MACHINE_TYPE);
#ifdef P4_DPRINTFL
        printf("   P4_DPRINTFL is:  on\n");
#else
        printf("   P4_DPRINTFL is:  off\n");
#endif
#ifdef ALOG_TRACE
        printf("    ALOG_TRACE is:  on\n");
#else
        printf("    ALOG_TRACE is:  off\n");
#endif
#if defined(SYSV_IPC)
        printf("      SYSV IPC is:  on\n");
#else
        printf("      SYSV IPC is:  off\n");
#endif
#if defined(VENDOR_IPC)
        printf("      VENDOR IPC is:  on\n");
#else
        printf("      VENDOR IPC is:  off\n");
#endif
        printf("\n");
}
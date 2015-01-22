/***************************************************************************
** Name         : CsdnBlogSpider.c
** Author       : xhjcehust
** Version      : v1.0
** Date         : 2015-01
** Description  : Download CSDN Blog. 
** This program can backup everybody's csdn blog.
**
** CSDN Blog    : http://blog.csdn.net/xhjcehust
** E-mail       : hjxiaohust@gmail.com
**
** This file may be redistributed under the terms
** of the GNU Public License.
***************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <netdb.h>
#include <pthread.h>
#include "csdnBlogSpider.h"

typedef struct __blog_info {
	char *b_title;
	char *b_date;
	int   b_reads;
	int   b_comments;
	int   b_download;
	int   b_seq_num;
} blog_info;

typedef struct {
	blog_info blog;
	char *b_url;
	char *b_host;
	int   b_port;
	int   b_sockfd;
	char *b_page_file;
	char *b_local_file;
	char continuous_rnbytes;
} blog_spider;

typedef struct __spider_work {
	blog_spider spider;
	struct __spider_work *next;
} spider_work;

static struct hostent *web_host;
static char illegal[] = {'\\', '/', ':', '*', '?', '\"', '<', '>', '|'};

static char *strrstr(const char *s1, const char *s2)
{
	int len2;
	char *ps1;

	if (!(len2 = strlen(s2))) {
		return (char *)s1;
	}
	
	ps1 = (char *)s1 + strlen(s1) - 1;
	ps1 = ps1 - len2 + 1;

	while (ps1 >= s1) {
		if ((*ps1 == *s2) && (strncmp(ps1, s2, len2) == 0)) {
			return (char *)ps1;
		}
		ps1--;
	}

	return NULL;
}

/* remove illegal character in title */
void title2filename(char *title)
{
	char *dst, *src;
	dst = src = title;
	int i, len = sizeof(illegal) / sizeof(illegal[0]);
	
	while (*src) {
		for (i = 0; i < len; i++) {
			if (illegal[i] == *src)
				break;
		}
		if (i == len)
			*dst++ = *src;
		src++;
	}
	if (dst == title) {
		SPIDER_DEBUG(SPIDER_LEVEL_INFO, "illegal blog title");
		*dst++ = '.';
	}
	*dst = '\0';
}

static void spider_work_insert(spider_work **head, spider_work *node)
{
	if (*head)
		node->next = *head;
	*head = node;
}

static void __init_spider(blog_spider *spider)
{
	memset(spider, 0, sizeof(*spider));
	spider->b_host          = CSDN_BLOG_HOST;
	spider->b_port          = CSDN_BLOG_PORT;
	spider->blog.b_download      = BLOG_UNDOWNLOAD;
}

static blog_spider *alloc_spider()
{
	blog_spider *spider = (blog_spider *)malloc(sizeof(blog_spider));
	if (NULL == spider)
		SPIDER_DEBUG(SPIDER_LEVEL_ERROR, "memory allocation failed\n");
	__init_spider(spider);
	return spider;
}

static void free_spider_item(blog_spider * spider)
{	
	if (!spider)
		return;
#define SPIDER_FREE_ITEM(item) do {  \
			if(spider->item)		 \
				free(spider->item); \
		} while(0)
	SPIDER_FREE_ITEM(blog.b_title);
	SPIDER_FREE_ITEM(blog.b_date);
	SPIDER_FREE_ITEM(b_url);
	SPIDER_FREE_ITEM(b_page_file);
	SPIDER_FREE_ITEM(b_local_file);
#undef SPIDER_FREE_ITEM
}

static int get_blog_info(blog_spider * spider, int *npages, char *csdn_id)
{
	FILE *fp;
	int count = 0;
	char *posA, *posB, *posC, *posD, *posE;
	char tmpbuf[BUFSIZE]   = {0};
	char tmpbuf2[BUFSIZE]  = {0};
	char line[BUFSIZE]     = {0};
	
	fp = fopen(spider->b_local_file, "r");
	if (NULL == fp)
		SPIDER_DEBUG(SPIDER_LEVEL_ERROR, "fopen %s failed: %s\n", 
			spider->b_local_file, strerror(errno));		

	/* blog title <a href="http://blog.csdn.net/id"> */
	sprintf(tmpbuf, "<a href=\"%s/%s\">", CSDN_BLOG_URL, csdn_id);
	while (fgets(line, sizeof(line), fp)) {
		posA = strstr(line, tmpbuf);
		
		if (posA) {
			posA += strlen(tmpbuf);
			posB = strstr(posA, "</a>");
			*posB = 0;
			spider->blog.b_title = strdup(posA);
			printf("CSDN ID : %s\nTITLE   : %s\nURL     : %s/%s\n",
				csdn_id, posA, CSDN_BLOG_URL, csdn_id);
			SPIDER_DEBUG(SPIDER_LEVEL_DEBUG, "b_title: %s\n", spider->blog.b_title);
			break;
		}
	}
	*npages = 0;
	while (fgets(line, sizeof(line), fp)) {
		posA = strstr(line, HTML_MULPAGE);
		
		if (posA) {
			fgets(line, sizeof(line), fp);
			posB = strrstr(line, BLOG_HREF);

			/* /gzshun/article/list/N, N is total_page_num */
			memset(tmpbuf, 0, sizeof(tmpbuf));
			sprintf(tmpbuf, "/%s/%s/", csdn_id, BLOG_NEXT_LIST);
			posB += strlen(BLOG_HREF) + strlen(tmpbuf);
			posC = strchr(posB, '"');
			*posC = 0;
			*npages = atoi(posB);
			spider->blog.b_seq_num = *npages;
			SPIDER_DEBUG(SPIDER_LEVEL_DEBUG, "b_page_total = %d\n", *npages);
			break;
		}
	}
 	if (*npages == 0) {
		fseek(fp, 0, SEEK_SET);
		*npages = 1;
	}
	while (fgets(line, sizeof(line), fp)) {
		posA = strstr(line, BLOG_RANK);

		if (posA) {
			count = 0;
			while (fgets(line, sizeof(line), fp)) {
				posB = strstr(line, BLOG_LI);
				if (posB) {
					if (7 == count) {
						break;
					}
					posB += strlen(BLOG_LI);
					posC = strstr(posB, BLOG_SPAN_HEAD);
					posD = posC + strlen(BLOG_SPAN_HEAD);
					posE = strstr(posD, BLOG_SPAN_END);
					*posC = 0;
					*posE = 0;
					memset(tmpbuf, 0, sizeof(tmpbuf));
					memset(tmpbuf2, 0, sizeof(tmpbuf2));
					strcpy(tmpbuf, posB);
					strcpy(tmpbuf2, posD);
					strcat(tmpbuf, tmpbuf2);
					count++;
					printf("%s\n", tmpbuf);
				}
			}	
			break;
		}
	}
	fclose(fp);
	return (count == 7) ? 0 : -1;
}

static int get_web_host(const char * hostname)
{
	/*get host ip*/
	web_host = gethostbyname(hostname);
	if (NULL == web_host)
		SPIDER_DEBUG(SPIDER_LEVEL_ERROR, "gethostbyname failed: %s\n", strerror(errno));

	SPIDER_DEBUG(SPIDER_LEVEL_DEBUG, "IP: %s\n", inet_ntoa(*((struct in_addr *)web_host->h_addr_list[0])));
	return 0;
}

void setnonblocking(int fd)
{
	int opts;
	opts = fcntl(fd, F_GETFL);
	if (opts < 0)
		SPIDER_DEBUG(SPIDER_LEVEL_ERROR, "fcntl failed: %s\n", strerror(errno));

	opts = opts | O_NONBLOCK;
	if(fcntl(fd, F_SETFL, opts) < 0)
		SPIDER_DEBUG(SPIDER_LEVEL_ERROR, "fcntl failed: %s\n", strerror(errno));
}

static int connect_web(blog_spider * spider)
{	
	int ret;
	struct sockaddr_in server_addr;

	/*init socket*/
	spider->b_sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (spider->b_sockfd < 0)
		SPIDER_DEBUG(SPIDER_LEVEL_ERROR, "socket failed: %s\n", strerror(errno));

	memset(&server_addr, 0, sizeof(server_addr));
	
	server_addr.sin_family	= AF_INET;
	server_addr.sin_port	= htons(spider->b_port);
	server_addr.sin_addr	= *((struct in_addr *)web_host->h_addr_list[0]);

	ret = connect(spider->b_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
	if (ret < 0)
		SPIDER_DEBUG(SPIDER_LEVEL_DEBUG, "connect failed: %s\n", strerror(errno));

	return ret;
}

static int send_request(const blog_spider * spider)
{
	int ret;
	char request[BUFSIZE];
	
	memset(request, 0, sizeof(request));
	sprintf(request, 
		"GET %s HTTP/1.1\r\n"
		"Accept: */*\r\n"
		"Accept-Language: zh-cn\r\n"
		"User-Agent: Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0)\r\n"
		"Host: %s:%d\r\n"
		"Connection: Close\r\n"
		"\r\n", spider->b_page_file, spider->b_host, spider->b_port);

	ret = send(spider->b_sockfd, request, sizeof(request), 0);
	if (ret < 0)
		SPIDER_DEBUG(SPIDER_LEVEL_INFO, "send failed: %s\n", strerror(errno));

	SPIDER_DEBUG(SPIDER_LEVEL_DEBUG, "request:\n%s\n", request);
	return ret;
}

static int read_sock_to_file(int sockfd, char *filename, int blocking, char *continuous_rnbytes)
{
	int recvsize, count, ret = 0;
	char recvbuf[BUFSIZE];
	FILE *fp;

	fp = fopen(filename, "a+");
	if (NULL == fp)
		SPIDER_DEBUG(SPIDER_LEVEL_ERROR, "fopen %s failed: %s\n",
			filename, strerror(errno));
	recvsize = 1;
	while (1) {
		count = read(sockfd, recvbuf, recvsize);
		if(count == 0) 
			break;
		else if(count < 0) {
			if (blocking && errno == EINTR)
				continue;
			else if (!blocking && errno == EWOULDBLOCK) {
				ret = -1;
				break;
			}
			SPIDER_DEBUG(SPIDER_LEVEL_ERROR, "read sock %d error\n", sockfd);
		}
		if(*continuous_rnbytes < 4) {
			if(recvbuf[0] == '\r' || recvbuf[0] == '\n') {
				(*continuous_rnbytes)++;
			} else {
				*continuous_rnbytes = 0;
			}
			/* omit http header by continuous "\r\n\r\n", namely, a blank line */
		} else {
			recvsize = sizeof(recvbuf) - 1;
			recvbuf[count] = '\0';
			fputs(recvbuf, fp);
		}
	}
	fclose(fp);
	return ret;
}

static void recv_response(blog_spider * spider)
{
	read_sock_to_file(spider->b_sockfd, spider->b_local_file,
		1, &spider->continuous_rnbytes);
	spider->blog.b_download = BLOG_DOWNLOAD;
	close(spider->b_sockfd);
}

blog_spider *get_spider_bysock(spider_work *work, int sockfd)
{
	while (work) {
		if (work->spider.b_sockfd == sockfd)
			return &work->spider;
		work = work->next;
	}
	return NULL;
}

static void epoll_recv_response(spider_work *work, int epollfd, int nworks, int timeout)
{
	int nfds, i;
	blog_spider *spider;
	struct epoll_event ev, *events;

	events = malloc(nworks * sizeof(*events));
	if (!events)
		SPIDER_DEBUG(SPIDER_LEVEL_ERROR, "memory allocation failed\n");
	while (1) {
		nfds = epoll_wait(epollfd, events, nworks, timeout);
		if (nfds < 0) {
			SPIDER_DEBUG(SPIDER_LEVEL_ERROR, "select failed: %s\n", strerror(errno));
			goto out;
		} else if (0 == nfds) {
			SPIDER_DEBUG(SPIDER_LEVEL_INFO, "download timeout, trying to download again\n");
			goto out;
		}
		for(i = 0; i < nfds; i++) {
	        if (events[i].events & EPOLLIN) {
				spider = get_spider_bysock(work, events[i].data.fd);
				assert(spider);
				if (read_sock_to_file(spider->b_sockfd, spider->b_local_file,
					0, &spider->continuous_rnbytes) < 0)
					continue;
				spider->blog.b_download = BLOG_DOWNLOAD;
				ev.events = EPOLLIN | EPOLLET;
			    ev.data.fd = spider->b_sockfd;
			    epoll_ctl(epollfd, EPOLL_CTL_DEL, spider->b_sockfd, &ev);
				if (spider->blog.b_date && spider->blog.b_title)
					SPIDER_DEBUG(SPIDER_LEVEL_INFO, "%-10s  ==>  %s  %s\n", "Download",
					   spider->blog.b_date, spider->blog.b_title);
				if (--nworks == 0)
					goto out;
			}
		}
	}
out:
	free(events);
}

void * download_blogs(void *arg)
{
	spider_work *work = arg, *iter;
	spider_work *next, *undownloaded_work;
	blog_spider *spider;
	struct epoll_event ev;
	int ret, epollfd, nworks, nretries = 0, timeout = 15000;

	while (work && nretries++ < MAX_RETRIES) {
		undownloaded_work = NULL;
		nworks = 0;
		epollfd = epoll_create(MAXFDS);
		iter = work;
		while (iter) {
			nworks++;
			spider = &iter->spider;
			ret = connect_web(spider);
			if (ret < 0)
				continue;
			ret = send_request(spider);
			if (ret < 0) {
				close(spider->b_sockfd);
				continue;
			}
			setnonblocking(spider->b_sockfd);
			ev.events = EPOLLIN | EPOLLET;
		    ev.data.fd = spider->b_sockfd;
		    epoll_ctl(epollfd, EPOLL_CTL_ADD, spider->b_sockfd, &ev);
			/* enough works has been fetched */
			if (nworks == MAXFDS) {
				undownloaded_work = iter->next;
				iter->next = NULL;
			}
			iter = iter->next;
		}
		epoll_recv_response(work, epollfd, nworks, timeout);
		close(epollfd);
		iter = work;
		while (iter) {
			next = iter->next;
			close(iter->spider.b_sockfd);
			if (iter->spider.blog.b_download != BLOG_DOWNLOAD) {
				if (iter->spider.blog.b_date && iter->spider.blog.b_title)
					SPIDER_DEBUG(SPIDER_LEVEL_INFO, "%-10s  ==>  %s  %s\n",
						"UnDownload", iter->spider.blog.b_date, iter->spider.blog.b_title);
				iter->next = NULL;
				spider_work_insert(&undownloaded_work, iter);
			} else {
				free_spider_item(&iter->spider);
				free(iter);
			}
			iter = next;
		}
		work = undownloaded_work;
		timeout <<= 1;
	}
	if (work)
		SPIDER_DEBUG(SPIDER_LEVEL_ERROR, 
			"download is not completed, please check your network\n");
	return NULL;
}

void multithread_download_blogs(spider_work *blog_spider_works, int num_threads)
{
	pthread_t *pids;
	spider_work **works, *next;
	int i;

	if (!blog_spider_works)
		return;
	if (num_threads < 2) {
		download_blogs(blog_spider_works);
		return;
	}
	works = malloc(num_threads * sizeof(*works));
	memset(works, 0, num_threads * sizeof(*works));
	/* dispatch spider work to different lists, ie, different threads */
	i = 0;
	while (blog_spider_works) {
		next = blog_spider_works->next;
		blog_spider_works->next = NULL;
		spider_work_insert(&works[i], blog_spider_works);
		i = (i + 1) % num_threads;
		blog_spider_works = next;
	}
	pids = malloc((num_threads - 1) * sizeof(*pids));
	for (i = 0; i < num_threads - 1; i++) {
        if (pthread_create(&pids[i], NULL, download_blogs, 
                    works[i]) != 0)
			SPIDER_DEBUG(SPIDER_LEVEL_ERROR, "pthread_create failed\n");
    }
	download_blogs(works[i]);
	free(works);
	for (i = 0; i < num_threads - 1; i++)
        pthread_join(pids[i], NULL);
	free(pids);
}

static int __download_index(blog_spider * spider)
{
	int ret;

	ret = connect_web(spider);
	if (ret < 0)
		goto fail;
	ret = send_request(spider);
	if (ret < 0)
		goto fail;
	recv_response(spider);
	return 0;
fail:
	close(spider->b_sockfd);
	return -1;
}

static int download_index(char *url, char *csdn_id, int *npages) 
{
	blog_spider *spider;
	char tmpbuf[BUFSIZE];
	int retval = 0;

	spider = alloc_spider();
	if (NULL == spider)
		return -1;

	spider->b_url = strdup(url);
	spider->b_local_file = malloc(strlen(BLOG_INDEX) + strlen(HTML) + 1);
	sprintf(spider->b_local_file, "%s%s", BLOG_INDEX, HTML);
	memset(tmpbuf, 0, sizeof(tmpbuf));
	sprintf(tmpbuf, "/%s", csdn_id);
	spider->b_page_file = strdup(tmpbuf);

	retval = get_web_host(CSDN_BLOG_HOST);
	if (retval < 0)
		goto fail;
	retval = __download_index(spider);
	if (retval < 0)
		goto fail;
	retval = get_blog_info(spider, npages, csdn_id);
	if (retval < 0) {
		unlink(spider->b_local_file);
		chdir("..");
		rmdir(csdn_id);
		SPIDER_DEBUG(SPIDER_LEVEL_INFO, "input csdn_id is not found\n");
		goto fail;
	}
fail:
	free_spider_item(spider);
	free(spider);
	return retval;
}
static void download_subindex(int npages, char *url, char *csdn_id)
{
	int i;
	char tmpbuf[BUFSIZE], stri[11];
	spider_work *work;
	spider_work *index_spider_works = NULL;

	for (i = 1; i <= npages; i++) {
		work = malloc(sizeof(*work));
		if (NULL == work)
			SPIDER_DEBUG(SPIDER_LEVEL_ERROR, "memory allocation failed\n");
		memset(tmpbuf, 0, sizeof(tmpbuf));
		sprintf(tmpbuf, "/%s/%s/%d", csdn_id, BLOG_NEXT_LIST, i);
		__init_spider(&work->spider);
		work->spider.b_page_file = strdup(tmpbuf);
		sprintf(stri, "%d", i);
		work->spider.b_local_file = malloc(strlen(BLOG_INDEX) + strlen(HTML) + strlen(stri) + 1);
		sprintf(work->spider.b_local_file, "%s%s%s", BLOG_INDEX, stri, HTML);
		work->spider.b_url = strdup(url);
		work->next = NULL;
		spider_work_insert(&index_spider_works, work);
	}
	download_blogs(index_spider_works);
}

static void update_subindexhtml(char *url, char *csdn_id, int npages)
{
	DIR *dp;
	struct dirent *dirp;

	if (NULL == (dp = opendir(".")))
		SPIDER_DEBUG(SPIDER_LEVEL_ERROR, "opendir failed\n");
	while ((dirp = readdir(dp)) != NULL) {
		if ((strcmp(dirp->d_name, ".") == 0)
			|| (strcmp(dirp->d_name, "..") == 0))
			continue;
		unlink(dirp->d_name);
	}
	closedir(dp);
	download_subindex(npages, url, csdn_id);
}

static void analyse_index(char *filename, spider_work **blog_spider_works, int *seq_num)
{
	FILE *fp;
	spider_work *work;
	int i, reads, comments;
	char *posA, *posB, *posC;
	char line[BUFSIZE], tmpbuf[BUFSIZE], title[BUFSIZE];
	char page_file[BUFSIZE], url[BUFSIZE], raw_title[BUFSIZE];
	char date[BUFSIZE];

	fp = fopen(filename, "r");
	if (fp == NULL)
		SPIDER_DEBUG(SPIDER_LEVEL_ERROR, "fopen %s failed: %s\n",
			filename, strerror(errno));		
	chdir("..");
	while (fgets(line, sizeof(line), fp)) {
		posA = strstr(line, HTML_ARTICLE);
		if (!posA)
			continue;

		/* blog url */
		posA += strlen(HTML_ARTICLE) + strlen(BLOG_HREF);
		posB = strchr(posA, '"');
		*posB = 0;
		memset(page_file, 0, sizeof(page_file));
		memset(url, 0, sizeof(url));
		strcpy(page_file, posA);
		sprintf(url, "%s%s", CSDN_BLOG_URL, posA);
		/* blog title */
		fgets(line, sizeof(line), fp);
		i = 0;
		while (1) {
			if (line[i] != ' ') {
				memset(raw_title, 0, sizeof(raw_title));
				line[strlen(line) - 2] = 0;    //erase '\r'&'\n'
				strcpy(raw_title, line + i);
				break;
			}
			i++;
		} 
		/* blog publish date*/
		while (fgets(line, sizeof(line), fp)) {
			posA = strstr(line, BLOG_DATE);
			
			if (!posA)
				continue;
			posA += strlen(BLOG_DATE);
			posB = strstr(posA, BLOG_SPAN_END);
			*posB = 0;
			memset(date, 0, sizeof(date));
			strcpy(date, posA);
			break;
		}
		/* blog read times */
		while (fgets(line, sizeof(line), fp)) {
			posA = strstr(line, BLOG_READ);

			if (!posA) 
				continue;
			posA += strlen(BLOG_READ);
			posB = strchr(posA, '(') + 1;
			posC = strchr(posB, ')');
			*posC = 0;
			reads = atoi(posB);
			break;
		}
		/* blog comment times */
		while (fgets(line, sizeof(line), fp)) {
			posA = strstr(line, BLOG_COMMENT);

			if (!posA)
				continue;
			posA += strlen(BLOG_COMMENT);
			posB = strchr(posA, '(') + 1;
			posC = strchr(posB, ')');
			*posC = 0;
			comments = atoi(posB);
			break;
		}
		(*seq_num)++;
		memset(tmpbuf, 0, sizeof(tmpbuf));
		snprintf(tmpbuf, sizeof(tmpbuf), "%d.%s", *seq_num, raw_title);
		strcpy(title, tmpbuf);
		snprintf(tmpbuf, sizeof(tmpbuf), "%s.html", title);
		title2filename(tmpbuf);
		if (access(tmpbuf, F_OK) == 0)
			continue;
		work = malloc(sizeof(*work));
		if (NULL == work)
			SPIDER_DEBUG(SPIDER_LEVEL_ERROR, "memory allocation failed\n");
		__init_spider(&work->spider);
		work->next = NULL;
		work->spider.b_page_file   = strdup(page_file);
		work->spider.b_url         = strdup(url);
		work->spider.blog.b_date        = strdup(date);
		work->spider.blog.b_reads       = reads;
		work->spider.blog.b_comments    = comments;
		work->spider.blog.b_seq_num     = *seq_num;
		work->spider.blog.b_title = strdup(title);
		work->spider.b_local_file  = strdup(tmpbuf);
		spider_work_insert(blog_spider_works, work);
	}
	chdir(BLOG_INDEX);
	fclose(fp);
}

static spider_work *build_spider_works()
{
	DIR *dp;
	struct dirent *dirp;
	spider_work *blog_spider_works = NULL;
	int seq_num = 0;

	if (NULL == (dp = opendir(".")))
		SPIDER_DEBUG(SPIDER_LEVEL_ERROR, "opendir failed\n");
	while ((dirp = readdir(dp)) != NULL) {
		if ((strcmp(dirp->d_name, ".") == 0)
			|| (strcmp(dirp->d_name, "..") == 0))
			continue;
		analyse_index(dirp->d_name, &blog_spider_works, &seq_num);
	}
	closedir(dp);
	return blog_spider_works;
}

int main(int argc, char **argv)
{
	int i, npages = 0, num_threads;
	char url[BUFSIZE];
	char csdn_id[BUFSIZE];
	spider_work *blog_spider_works = NULL;

	if (argc != 2 && argc != 3) {
		fprintf(stderr, "Usage  :\n%s CSDN_ID [thread number]\n"
						"default thread number = NCPUS\n"
						"Example: %s xhjcehust\n", 
						argv[0], argv[0]);
		return -1;
	}
	if (argc == 3)
		num_threads = atoi(argv[2]);
	else
		num_threads = sysconf(_SC_NPROCESSORS_CONF) - 1;
	/* change id to lower case, just as csdn does */
	for (i = 0; argv[1][i] != '\0'; i++) {
		if (isalpha(argv[1][i]))
			csdn_id[i] = tolower(argv[1][i]);
		else
			csdn_id[i] = argv[1][i];
	}
	csdn_id[i] = '\0';
	memset(url, 0, sizeof(url));
	sprintf(url, "%s/%s", CSDN_BLOG_URL, csdn_id);
	if (access(csdn_id, F_OK) < 0)
		mkdir(csdn_id, 0755);
	chdir(csdn_id);
	if (download_index(url, csdn_id, &npages) < 0) {
		SPIDER_DEBUG(SPIDER_LEVEL_DEBUG, "download_index failed\n");
		return -1;
	}
	if (access(BLOG_INDEX, F_OK) < 0)
		mkdir(BLOG_INDEX, 0755);
	chdir(BLOG_INDEX);
	update_subindexhtml(url, csdn_id, npages);
	blog_spider_works = build_spider_works();
	chdir("..");
	multithread_download_blogs(blog_spider_works, num_threads);
	return 0;
}

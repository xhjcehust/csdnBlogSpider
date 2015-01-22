#ifndef __BLOGSPIDER_H__
#define __BLOGSPIDER_H__
#include <stdio.h>

/*Debug program macro*/
#define SPIDER_LEVEL_DEBUG 0
#define SPIDER_LEVEL_INFO  1
#define SPIDER_LEVEL_WARN  2
#define SPIDER_LEVEL_ERROR 3

#define SPIDER_DEBUG(level, ...) do {\
	if (level > SPIDER_LEVEL_DEBUG) {\
        fprintf(stdout, __VA_ARGS__);    \
        if (level == SPIDER_LEVEL_ERROR) \
        exit(1);    \
    }\
} while(0)

#define BUFSIZE		     512
#define MAXFDS           1024
#define MAX_RETRIES      5
#define NUM_WORKER_THREADS 12 

#define HTML_ARTICLE     ("<span class=\"link_title\">")
#define HTML_MULPAGE     ("class=\"pagelist\"")
#define BLOG_NEXT_LIST   ("article/list")
#define BLOG_TITLE       ("title=\"")
#define BLOG_HREF        ("<a href=\"")
#define BLOG_DATE        ("<span class=\"link_postdate\">")
#define BLOG_READ        ("<span class=\"link_view\"")
#define BLOG_COMMENT     ("<span class=\"link_comments\"")
#define BLOG_SPAN_HEAD   ("<span>")
#define BLOG_SPAN_END    ("</span>")
#define BLOG_RANK        ("blog_rank")
#define BLOG_LI          ("<li>")
#define BLOG_INDEX       ("index")
#define HTML             (".html")
#define CSDN_BLOG_URL    ("http://blog.csdn.net")
#define CSDN_BLOG_HOST   ("blog.csdn.net")
#define CSDN_BLOG_PORT   (80)

#define BLOG_LOCK        (10)
#define BLOG_UNLOCK      (11)
#define BLOG_DOWNLOAD    (20)
#define BLOG_UNDOWNLOAD  (21)

#endif

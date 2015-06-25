#!/usr/bin/env python
# -*- coding: utf-8 -*-
import re
import urllib2
import sys
import os

filename = 0
BLOG_URL = "http://blog.csdn.net"

def getHtml(url):
    try:
        req = urllib2.Request(url)
        req.add_header('User-Agent','Mozilla/4.0 (compatible; MSIE 5.01; Windows NT 5.0)')
        page = urllib2.urlopen(req, timeout = 5)
    except Exception, e:
		print str(e)
		return
    return page.read()

def findInfo(html, reg):
    regcom = re.compile(reg)
    retlist = re.findall(regcom, html)
    return retlist

def getTotalPages(homehtml, csdnid):
    reg = r'<a href="/%s/article/list/(\d)">尾页</a>' %csdnid
    totalpages = findInfo(homehtml, reg)
    if not totalpages:
        return 1  #just one page
    return int(totalpages[0])
    
def getBlogInfo(homehtml, homeurl):
    reg = r'<a href="%s">((.|\n)*?)</a>' %homeurl
    imglist = findInfo(homehtml, reg)
    if not imglist:
        return -1
    print "TITLE   :", imglist[0][0]
    print "URL     :", homeurl
    reg = r'<ul id="blog_rank">((.|\n)*?)</ul>'
    imglist = findInfo(homehtml, reg)
    if not imglist:
        sys.exit(0)
    substr = imglist[0][0]
    re = r'访问：<span>((.|\n)*?)</span>'
    print "访问：", findInfo(substr, re)[0][0]
    re = r'积分：<span>((.|\n)*?)</span>'
    print "积分：", findInfo(substr, re)[0][0]
    re = r'排名：<span>((.|\n)*?)</span>'
    print "排名：", findInfo(substr, re)[0][0]

    reg = r'<ul id="blog_statistics">((.|\n)*?)</ul>'
    imglist = findInfo(homehtml, reg)
    if not imglist:
        sys.exit(0)
    substr = imglist[0][0]
    re = r'原创：<span>((.|\n)*?)</span>'
    print "原创：", findInfo(substr, re)[0][0]
    re = r'转载：<span>((.|\n)*?)</span>'
    print "转载：", findInfo(substr, re)[0][0]
    re = r'译文：<span>((.|\n)*?)</span>'
    print "译文：", findInfo(substr, re)[0][0]
    re = r'评论：<span>((.|\n)*?)</span>'
    print "评论：", findInfo(substr, re)[0][0]
    return 0

def getSubBlog(html):
    reg = r'<span class="link_title">((.|\n)*?)</span>'
    imglist = findInfo(html, reg)
    if not imglist:
        return
    for item in imglist:
        bloglist = item[0].split(os.linesep)
        blogurl = BLOG_URL + bloglist[0].strip().split('"')[1]
        blogtitle = bloglist[1].strip()
        global filename
        filename += 1
        bloghtml = getHtml(blogurl)
        if bloghtml is None:
            print "Download    ==> %s failed" %blogtitle
            continue
        print "Download    ==> %s success" %blogtitle
        with open("%s.html" %filename, "w") as f:
            f.write(bloghtml)

def main(csdnid):
	homeurl = "%s/%s" %(BLOG_URL, csdnid)
	homehtml = getHtml(homeurl)
	if homehtml is None:
		return
	ret = getBlogInfo(homehtml, homeurl)
	if ret == -1:
		print "csdnid is not found!!!"
		sys.exit(0)
	dirname = csdnid
	if not os.path.exists(dirname):
		os.mkdir(dirname)
	os.chdir(dirname)
	getSubBlog(homehtml)
	totalpages = getTotalPages(homehtml, csdnid)
	for i in range(2, totalpages + 1):
		pageurl = "%s/%s/article/list/%d" %(BLOG_URL, csdnid, i)
		pagehtml = getHtml(pageurl)
		if pagehtml is None:
			continue
		getSubBlog(pagehtml)
	print "download %d blogs" %filename

if __name__ == "__main__":
    try:
        csdnid = raw_input("CSDN ID:")
        main(csdnid)
    except Exception, e:
        print str(e)
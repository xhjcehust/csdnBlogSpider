CC = cc
CFLAGS = -Wall -g
LIBS = -lpthread  
OBJS= csdnBlogSpider.o
TARGET = csdnBlogSpider
$(TARGET) : $(OBJS)
	$(CC) $(OBJS) -o $(TARGET) $(LIBS)
$(OBJS):csdnBlogSpider.c csdnBlogSpider.h
	$(CC) -c $(CFLAGS) csdnBlogSpider.c -o $@ 
clean:
	-rm -f csdnBlogSpider *.o
.PHONY:clean
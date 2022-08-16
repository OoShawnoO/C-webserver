#include "http_conn.h"

#define MAX_FD 65535
#define MAX_EVENTS_NUM 10000

// using namespace std;
//添加新号捕捉
void addsig(int sig,void(handler)(int)){
    struct sigaction sa;
    memset(&sa,'\0',sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig,&sa,NULL);
}

//添加文件描述符到epoll中
extern void addfd(int epollfd,int fd,bool one_shot);
extern void removefd(int epollfd,int fd);
extern void modfd(int epollfd,int fd,int ev);

int main(int argc,char* argv[]){
    if(argc <= 1){
        std::cout<< "按照如下格式运行:"<<basename(argv[0])<<"portnumber\n";
    }

    //获取端口号
    int port = atoi(argv[1]);
    
    //对SIGPIE信号进行处理;
    addsig(SIGPIPE,SIG_IGN);

    //创建线程池
    threadpool<http_conn>* pool = NULL;
    try{
        pool = new threadpool<http_conn>;
    }catch(...){
        exit(-1);
    }

    //创建一个数组用于保护所有的用户信息
    http_conn *users = new http_conn[MAX_FD];
    
    int listenfd = socket(PF_INET,SOCK_STREAM,0);

    //设置端口复用
    int reuse = 1;
    setsockopt(listenfd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));

    //绑定
    struct sockaddr_in address;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_family = PF_INET;
    address.sin_port = htons(port);
    bind(listenfd,(struct sockaddr*)&address,sizeof(address));

    //监听
    listen(listenfd,5);

    epoll_event events[MAX_EVENTS_NUM];

    int epollfd = epoll_create(5);

    //将监听的事件添加到epoll中
    addfd(epollfd,listenfd,false);
    http_conn::m_epollfd = epollfd;
    
    
    while(true){
        int num =epoll_wait(epollfd,events,MAX_EVENTS_NUM,-1);
        if((num<0)&&(errno!=EINTR)){
            std::cout << "epoll failure" <<std::endl;
            break;
        }
        for(int i=0;i<num;i++){
            int sockfd = events[i].data.fd;
            if(sockfd == listenfd){
                sockaddr_in clientaddr;
                socklen_t len = sizeof(clientaddr);
                int connfd = accept(listenfd,(struct sockaddr*)&clientaddr,&len);
                if(http_conn::m_user_count>=MAX_FD){
                    close(connfd);
                    continue;
                }
                //将新的客户的数据初始化
                users[connfd].init(connfd,clientaddr);

            }else if(events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)){
                //对方异常
                users[sockfd].close_conn();
            }else if(events[i].events & EPOLLIN){
                if(users[sockfd].read()){
                    std::cout << "pool append" << std::endl;
                    pool->append(users+sockfd);
                }else{
                    std::cout << "close_conn" << std::endl;
                    users[sockfd].close_conn();
                }
            }else if(events[i].events & EPOLLOUT){
                if(!users[sockfd].write()){
                    users[sockfd].close_conn();
                }
            }
           
        }
    }

    close(epollfd);
    close(listenfd);
    delete []users;
    delete pool;

    return 0;
}
#include "../http_worker.h"


//this function is run against the http request 
void cookie_test_gen(std::list<struct http_connection>::iterator &http_connection,size_t worker_id)
{

	struct HTTP_COOKIE cookie1,cookie2;

	init_http_cookie(&cookie1,"Foo","Bar");
	cookie1.http_only = true;
	cookie1.expires = time(NULL) + 86400; // one day


	init_http_cookie(&cookie2,"counter","1");
	cookie2.secure = http_connection->https;
	cookie2.max_age = 86400; // one day

	if(HTTP_COOKIE_EXISTS("counter"))
	{
		bool invalid_number;
		int counter = str2uint(&HTTP_COOKIE("counter"),&invalid_number);

		if(!invalid_number)
			cookie2.value = int2str(counter + 1);
	}

	if(!http_connection->response.COOKIES)
		http_connection->response.COOKIES = new std::vector<struct HTTP_COOKIE>();
	
	http_connection->response.COOKIES->push_back(cookie1);
	http_connection->response.COOKIES->push_back(cookie2);

	std::string html_ident = "<span style=\"margin-left:25px;\"></span>";

	echo("<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n</head>\n<body>\n");

	echo("COOKIE[] <br>{<br>");

	if(http_connection->request.COOKIES)
	{
		for(auto i=http_connection->request.COOKIES->begin(); i!=http_connection->request.COOKIES->end(); ++i)
		{
			echo(html_ident);
			echo(i->first);
	
			if(!i->second.empty())
			{
				echo(" => ");
				echo(i->second);
			}
	
			echo("<br>");
		}
	}

	echo("}<br><br>");


	echo("</body>\n</html>");
}

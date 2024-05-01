#include "../custom_bound.h"

//this function is run against the http request
int cookie_test_gen(const struct HTTP_CUSTOM_PAGE_HANDLER_ARGUMENTS& args)
{
	struct HTTP_COOKIE cookie1, cookie2;

	HTTP_Init_Cookie(&cookie1,"Foo","Bar");
	cookie1.http_only = true;
	cookie1.expires = time(NULL) + 86400; // one day

	HTTP_Init_Cookie(&cookie2,"counter","1");
	cookie2.secure = args.conn->https;
	cookie2.max_age = 86400; // one day

	if(HTTP_COOKIE_EXISTS("counter"))
	{
		bool invalid_number;
		int counter = str2uint(HTTP_COOKIE("counter"), &invalid_number);

		if(!invalid_number)
		{
			cookie2.value = int2str(counter + 1);
		}
	}

	if(!args.response->COOKIES)
	{
		args.response->COOKIES = new std::vector<struct HTTP_COOKIE>();
	}
	
	args.response->COOKIES->push_back(cookie1);
	args.response->COOKIES->push_back(cookie2);

	std::string html_ident = "<span style=\"margin-left:25px;\"></span>";

	echo("<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n</head>\n<body>\n");

	echo("COOKIE[] <br>{<br>");

	if(args.request->COOKIES)
	{
		for(auto i = args.request->COOKIES->begin(); i != args.request->COOKIES->end(); ++i)
		{
			echo(html_ident);
			echo("'");
			echo(i->first);
			echo("'");
	
			if(!i->second.empty())
			{
				echo(" => ");
				echo("'");
				echo(i->second);
				echo("'");
			}
	
			echo("<br>");
		}
	}

	echo("}<br><br>");
	echo("</body>\n</html>");

	return HTTP_CONNECTION_OK;
}

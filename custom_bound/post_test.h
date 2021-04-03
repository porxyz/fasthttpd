#include "../http_worker.h"
#include "../mod_mysql.h"


//this function is run against the http request 
void post_test_gen(std::list<struct http_connection>::iterator &http_connection,size_t worker_id)
{
	std::string html_ident = "<span style=\"margin-left:25px;\"></span>";

	echo("<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n</head>\n<body>\n");

	echo("GET_ARGS[] <br>{<br>");
	for(auto i=http_connection->request.URI_query.begin(); i!=http_connection->request.URI_query.end(); ++i)
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
	
	echo("}<br><br>");

	if(http_connection->request.request_method == HTTP_METHOD_POST)
	{
		echo("POST_ARGS[] <br>{<br>");
		for(auto i=http_connection->request.POST_query->begin(); i!=http_connection->request.POST_query->end(); ++i)
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
	
		echo("}<br><br>");


		if(http_connection->request.POST_type == HTTP_POST_MULTIPART_FORM_DATA)
		{
			echo("POST_FILES[] <br>{<br>");
		
			for(auto i=http_connection->request.POST_files->begin(); i!=http_connection->request.POST_files->end(); ++i)
			{
				echo(html_ident);
				echo(i->first);
				echo("[] <br>");
				echo(html_ident);
				echo("{<br>");

				for(auto j=i->second.begin(); j!=i->second.end(); ++j)
				{
					echo(html_ident);
					echo(html_ident);
					echo(j->first);
					echo(" => { type => ");
					echo(j->second.type);
					echo(" , data => string(");
					echo(std::to_string(j->second.data.size()));
					echo(") }<br>");	
				}

				echo(html_ident);
				echo("}<br>");
			}
	
			echo("}<br><br>");
		}

	}

	echo("</body>\n</html>");
}

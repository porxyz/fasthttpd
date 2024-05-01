#include "../custom_bound.h"

//this function is run against the http request 
int post_test_gen(const struct HTTP_CUSTOM_PAGE_HANDLER_ARGUMENTS& args)
{
	std::string html_ident = "<span style=\"margin-left:25px;\"></span>";

	echo("<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n</head>\n<body>\n");

	echo("GET_ARGS[] <br>{<br>");
	for(auto i = args.request->URI_query.begin(); i != args.request->URI_query.end(); ++i)
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

	if(args.request->method == HTTP_METHOD_POST)
	{
		echo("POST_ARGS[] <br>{<br>");
		for(auto i= args.request->POST_query->begin(); i!= args.request->POST_query->end(); ++i)
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

		if(args.request->POST_type == HTTP_POST_MULTIPART_FORM_DATA)
		{
			echo("POST_FILES[] <br>{<br>");
		
			for(auto i=args.request->POST_files->begin(); i != args.request->POST_files->end(); ++i)
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

	return HTTP_CONNECTION_OK;
}

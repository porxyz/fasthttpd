#ifndef _page_index_inc_
#define _page_index_inc_


#include "../http_worker.h"
#include "../mod_mysql.h"

#include <openssl/rand.h>
#include <openssl/aes.h>

std::string compute_hashed_password(const std::string& username,const std::string& password)
{
uint8_t hash_buffer[SHA256_DIGEST_LENGTH];

SHA256_CTX sha256_ctx;
SHA256_Init(&sha256_ctx);
SHA256_Update(&sha256_ctx,username.c_str(),username.size());
SHA256_Final(hash_buffer,&sha256_ctx);

std::string intermediate_value;
for(int i = 0; i < SHA256_DIGEST_LENGTH; i++)
{
std::string hex_value = int2str(hash_buffer[i],16);
if(hex_value.size() == 1){intermediate_value.append("0");}
intermediate_value.append(hex_value);
}

intermediate_value.append(password);
intermediate_value.append(username);

SHA256_Init(&sha256_ctx);
SHA256_Update(&sha256_ctx,intermediate_value.c_str(),intermediate_value.size());
SHA256_Final(hash_buffer,&sha256_ctx);

return std::string((char*)hash_buffer,SHA256_DIGEST_LENGTH);
}


void encrypt_server_response(const char* session_key,const char* session_iv,std::string* plaintext,std::string* ciphertext)
{

size_t padding_len = plaintext->size() % 16;
if(padding_len != 0){padding_len = 16 - padding_len;}
plaintext->append(padding_len,0);

uint8_t hash_buffer[SHA256_DIGEST_LENGTH];

SHA256_CTX sha256_ctx;
SHA256_Init(&sha256_ctx);
SHA256_Update(&sha256_ctx,plaintext->c_str(),plaintext->size());
SHA256_Final(hash_buffer,&sha256_ctx);

std::string raw_ciphertext = plaintext[0];

AES_KEY enc_key;
AES_set_encrypt_key((uint8_t*)session_key,32,&enc_key);
AES_cbc_encrypt((uint8_t*)plaintext->c_str(),(uint8_t*)raw_ciphertext.c_str(),plaintext->size(),&enc_key,(uint8_t*)session_iv,AES_ENCRYPT);


std::string hex_byte;
for(size_t i=0; i<SHA256_DIGEST_LENGTH; i++)
{
hex_byte = int2str(hash_buffer[i],16);
if(hex_byte.size() == 1){ciphertext->append("0");}
ciphertext->append(hex_byte);
}

for(size_t i=0; i<raw_ciphertext.size(); i++)
{
hex_byte = int2str((uint8_t)raw_ciphertext[i],16);
if(hex_byte.size() == 1){ciphertext->append("0");}
ciphertext->append(hex_byte);
}


}


/*
void generate_user_session(uint64_t username_id,std::unordered_map)
{
$session_key = bin2hex(random_bytes(32));
$session_iv = bin2hex(random_bytes(16));
return array("user_id" => $username_id, "key" => $session_key, "iv" => $session_iv);
}
*/

bool update_user_session(const void* mysql_conn,uint64_t session_id)
{
std::string query_string = "UPDATE user_sessions SET last_action = UNIX_TIMESTAMP() WHERE ID = ";
query_string.append(int2str(session_id));

mysql_stmt_query db_query(mysql_conn);

if(db_query.query_handle == NULL or db_query.get_last_errno() != 0){return false;}

db_query.prepare(query_string.c_str());
if(db_query.get_last_errno() != 0){return false;}

return db_query.execute();
}

bool update_user_last_action(const void* mysql_conn,uint64_t user_id)
{
std::string query_string = "UPDATE users SET last_action = UNIX_TIMESTAMP() WHERE ID = ";
query_string.append(int2str(user_id));

mysql_stmt_query db_query(mysql_conn);

if(db_query.query_handle == NULL or db_query.get_last_errno() != 0){return false;}

db_query.prepare(query_string.c_str());
if(db_query.get_last_errno() != 0){return false;}

return db_query.execute();
}


void index_gen(std::list<struct http_connection>::iterator http_connection,size_t worker_id)
{

// sanity check
if(http_connection->request.request_method != HTTP_METHOD_POST)
{
http_connection->response.response_code = 400;
echo("Invalid request method!");
return;
}

/*
if(http_connection->https !== "on"){http_connection->response.response_code = 400; echo("Only HTTPS requests possible!"); return;}
*/

if(!(HTTP_POST_ARG_EXISTS("username")) or !(HTTP_POST_ARG_EXISTS("password")))
{
echo("{\"status\":\"error\",\"code\":0}");
return;
}

if(HTTP_POST_ARGV("username").size() < 5 or HTTP_POST_ARGV("username").size() > 30 or HTTP_POST_ARGV("password").size() < 6 or HTTP_POST_ARGV("password").size() > 32)
{
echo("{\"status\":\"error\",\"code\":1}");
return;
}

std::string hashed_password = compute_hashed_password(HTTP_POST_ARGV("username"),HTTP_POST_ARGV("password"));

/* 
if(!http_workers[worker_id].mysql_db_handle->is_alive()) // if MOD_MYSQL autoreconnect is disabled then uncomment this block
{
http_workers[worker_id].mysql_db_handle->reset();
echo("NO MYSQL DB CONNECTION"); return;
}
*/


mysql_stmt_query db_query(http_workers[worker_id].mysql_db_handle);

if(db_query.query_handle == NULL or db_query.get_last_errno() != 0)
{
echo("{\"status\":\"error\",\"code\":2}");
return;
}

db_query.prepare("SELECT ID,hashed_password FROM users WHERE username = ?",1,2);
if(db_query.get_last_errno() != 0)
{
echo("{\"status\":\"error\",\"code\":2}");
return;
}


size_t username_buffer_size = HTTP_POST_ARGV("username").size();
db_query.bind_param(0,MYSQL_TYPE_STRING,(char*)HTTP_POST_ARGV("username").c_str(),false,&username_buffer_size);

if(!db_query.execute())
{
echo("{\"status\":\"error\",\"code\":2}");
return;
}

uint64_t user_ID;
char db_hashed_password[SHA256_DIGEST_LENGTH]; 

db_query.bind_result(0,MYSQL_TYPE_LONGLONG,&user_ID);
db_query.bind_result(1,MYSQL_TYPE_BLOB,db_hashed_password,NULL,NULL,SHA256_DIGEST_LENGTH);

db_query.store_result();

if(db_query.num_rows() != 1)
{
echo("{\"status\":\"error\",\"code\":3}");
return;
}

db_query.fetch();

if(hashed_password != std::string(db_hashed_password,SHA256_DIGEST_LENGTH))
{
echo("{\"status\":\"error\",\"code\":4}");
return;
}


if(!update_user_last_action(http_workers[worker_id].mysql_db_handle,user_ID))
{
echo("{\"status\":\"error\",\"code\":2}");
return;
}

uint8_t session_key[32];
uint8_t session_iv[16];

RAND_bytes(session_key,32);
RAND_bytes(session_iv,16);

std::string user_agent;
if(http_connection->request.request_headers.find("User-Agent") == http_connection->request.request_headers.end()){user_agent = " ";}
else{user_agent = http_connection->request.request_headers["User-Agent"];} 
if(user_agent.size() > 64){user_agent = user_agent.substr(0,64);}


mysql_stmt_query insert_query(http_workers[worker_id].mysql_db_handle);

if(insert_query.query_handle == NULL or insert_query.get_last_errno() != 0)
{
echo("{\"status\":\"error\",\"code\":2}");
return;
}

insert_query.prepare("INSERT INTO user_sessions VALUES (NULL,?,?,?,UNIX_TIMESTAMP(),?,?,UNIX_TIMESTAMP())",5);
if(db_query.get_last_errno() != 0)
{
echo("{\"status\":\"error\",\"code\":2}");
return;
}


size_t session_key_buffer_size = 32;
size_t session_iv_buffer_size = 16;
size_t user_agent_buffer_size = user_agent.size();
size_t remote_addr_buffer_size = http_connection->remote_addr.size();

insert_query.bind_param(0,MYSQL_TYPE_LONGLONG,&user_ID);
insert_query.bind_param(1,MYSQL_TYPE_BLOB,session_key,false,&session_key_buffer_size);
insert_query.bind_param(2,MYSQL_TYPE_BLOB,session_iv,false,&session_iv_buffer_size);
insert_query.bind_param(3,MYSQL_TYPE_STRING,(char*)user_agent.c_str(),false,&user_agent_buffer_size);
insert_query.bind_param(4,MYSQL_TYPE_STRING,(char*)http_connection->remote_addr.c_str(),false,&remote_addr_buffer_size); // remote addr

if(!insert_query.execute())
{
echo("{\"status\":\"error\",\"code\":2}");
return;
}

std::string session_id = int2str(http_workers[worker_id].mysql_db_handle->last_insert_id());

std::string session_proof;

encrypt_server_response((char*)session_key,(char*)session_iv,&session_id,&session_proof);

echo("{\"status\":\"success\",\"session\":{\"user_id\":");
echo(int2str(user_ID)); echo(",\"key\":\"");

std::string hex_byte;
for(size_t i=0; i<32; i++)
{
hex_byte = int2str(session_key[i],16);
if(hex_byte.size() == 1){echo("0");}
echo(hex_byte);
}

echo("\",\"iv\":\"");

for(size_t i=0; i<16; i++)
{
hex_byte = int2str(session_iv[i],16);
if(hex_byte.size() == 1){echo("0");}
echo(hex_byte);
}

echo("\",\"id\":"); echo(session_id); echo(",\"proof\":\""); echo(session_proof);
echo("\"}}");
}


#endif

import sys

import openai

def main():
    openai.api_key = "sk-Ya0zEH6mnbBPR10IT3BlbkFJpwbhUvomhMKaJ0WI"
    #print(openai.Model.list())
    #args = "What is the capital of France?"
    args = sys.argv[1]
    response = openai.ChatCompletion.create(
        model='gpt-3.5-turbo',
        messages=[{"role": "system", "content": "You are a helpful assistant."},
                {"role": "user", "content": args}]
        #prompt = prompt,
        #max_tokens=50,
        #n=1,
        #stop=None,
        #temperature=0.7
    )
    print(response.choices[0].message["content"])
    #context = "You are chatting with a customer service representative."
    #message = "Hi, I have a problem with my account."
    #response = openai.Completion.create(
    #  engine="gpt-3.5-turbo",
    #  prompt=f"Chat:\n{context}\nUser: {message}\n",
    #  max_tokens=50
    #)
    #reply = response.choices[0].text.strip()
    #args = sys.argv[1]
    #print(reply)

if __name__ == "__main__":
    main()
import os
import sys

from langchain.vectorstores import Chroma
from langchain.embeddings import OpenAIEmbeddings
from langchain.indexes.vectorstore import VectorStoreIndexWrapper
from langchain.document_loaders import DirectoryLoader
from langchain.indexes import VectorstoreIndexCreator
from langchain.chains import RetrievalQA
from langchain.chat_models import ChatOpenAI

import constants

os.environ["OPENAI_API_KEY"] = constants.APIKEY


def main():
    query = sys.argv[1]

    # Enable to save to disk & reuse the model (for repeated queries on the same data)
    PERSIST = True
    #persist_dir = "D:\\DedaFix\\SWGSolo\\MMOCoreORB\\bin\\scripts\\persist"
    persist_dir = "/home/swgemu/Core3/MMOCoreORB/bin/scripts/persist"
    #if PERSIST and os.path.exists("/home/swgemu/Core3/MMOCoreORB/bin/scripts/persist"):
    #if PERSIST and os.path.exists("D:\\DedaFix\\SWGSolo\\MMOCoreORB\\bin\\conf\\persist"):
    if PERSIST and os.path.exists(persist_dir):
      print("Reusing index...\n")
      vectorstore = Chroma(persist_directory=persist_dir, embedding_function=OpenAIEmbeddings())
      index = VectorStoreIndexWrapper(vectorstore=vectorstore)
    else:
      #loader = TextLoader("data/data.txt") # Use this line if you only need data.txt
      loader = DirectoryLoader("/home/swgemu/Core3/MMOCoreORB/bin/scripts/data_files/")
      #loader = DirectoryLoader("D:\\DedaFix\\SWGSolo\\MMOCoreORB\\bin\\scripts\\data_files\\")
      if PERSIST:
        index = VectorstoreIndexCreator(vectorstore_kwargs={"persist_directory":persist_dir}).from_loaders([loader])
      else:
        index = VectorstoreIndexCreator().from_loaders([loader])

    chain = RetrievalQA.from_chain_type(
      llm=ChatOpenAI(model="gpt-3.5-turbo"),
      retriever=index.vectorstore.as_retriever(search_kwargs={"k": 1}),
    )
    print(chain.run(query))

if __name__ == "__main__":
    main()
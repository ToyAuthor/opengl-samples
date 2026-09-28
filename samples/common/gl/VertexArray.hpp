#pragma once

#include <vector>
#include <memory>
#include "gl/Core.hpp"
#include "gl/VertexArrayBindingIndex.hpp"
#include "gl/ElementsBuffer.hpp"

namespace gl{

// 包裝 VAO(Vertex Array Object)的 DSA 操作
// 使用 RAII 管理生命週期，禁止複製、允許移動
// 程式從頭到尾只使用一個 VAO 是可行的
class VertexArray
{
	public:

		/*
		 * 這邊直接寫死 binding index 數量，我認為這種名字固定的寫法更適合
		 * index 數字幾乎就像個名字一樣，那個數字沒意義
		 */
		VertexArrayBindingIndex bindingIndex0;
		VertexArrayBindingIndex bindingIndex1;
		VertexArrayBindingIndex bindingIndex2;
		VertexArrayBindingIndex bindingIndex3;
		VertexArrayBindingIndex bindingIndex4;
		VertexArrayBindingIndex bindingIndex5;

		VertexArray( std::shared_ptr<::gl::Core> core ) :
			_core( core ),
			bindingIndex0( 0 ),
			bindingIndex1( 1 ),
			bindingIndex2( 2 ),
			bindingIndex3( 3 ),
			bindingIndex4( 4 ),
			bindingIndex5( 5 )
		{
			glCreateVertexArrays( 1, &_id );

			// 賦值之後就不能更改了
			bindingIndex0._VAO = _id;
			bindingIndex1._VAO = _id;
			bindingIndex2._VAO = _id;
			bindingIndex3._VAO = _id;
			bindingIndex4._VAO = _id;
		}

		~VertexArray()
		{
			release();
		}

		// 禁用複製
		VertexArray( const VertexArray& ) = delete;
		VertexArray& operator=( const VertexArray& ) = delete;

		// 允許移動
		VertexArray( VertexArray&& rhs ) noexcept:
			_core( rhs._core ),
			_id( rhs._id ),
			bindingIndex0( 0 ),
			bindingIndex1( 1 ),
			bindingIndex2( 2 ),
			bindingIndex3( 3 ),
			bindingIndex4( 4 ),
			bindingIndex5( 5 )
		{
			rhs._id = 0;
			rhs._core = nullptr;

			// 賦值之後就不能更改了
			bindingIndex0 = rhs.bindingIndex0;
			bindingIndex1 = rhs.bindingIndex1;
			bindingIndex2 = rhs.bindingIndex2;
			bindingIndex3 = rhs.bindingIndex3;
			bindingIndex4 = rhs.bindingIndex4;
			bindingIndex5 = rhs.bindingIndex5;
		}

		VertexArray& operator=( VertexArray&& rhs ) noexcept
		{
			if ( this != &rhs )
			{
				release();
				_core   = rhs._core;
				_id     = rhs._id;
				rhs._id = 0;
				rhs._core = nullptr;

				// 賦值之後就不能更改了
				bindingIndex0 = rhs.bindingIndex0;
				bindingIndex1 = rhs.bindingIndex1;
				bindingIndex2 = rhs.bindingIndex2;
				bindingIndex3 = rhs.bindingIndex3;
				bindingIndex4 = rhs.bindingIndex4;
				bindingIndex5 = rhs.bindingIndex5;
			}

			return *this;
		}

		void bindEBO( std::shared_ptr<gl::ElementsBuffer> EBO )
		{
			// 確保 EBO 的生命週期會陪著 VAO 一起走
			_EBO = EBO;
			glVertexArrayElementBuffer( _id, _EBO->getID() );
		}

		// 綁定此 VAO 為目前使用的頂點陣列
		void bind() const
		{
			_core->_bindVAO( _id );
		}

		/*
		 * 解除綁定(切換回預設的 0 號 VAO)
		 * 不過這功能實在沒必要
		 * 因為 OpenGL 規定描繪時就是一定要綁一個 VAO
		 * 即使不需要 VAO 進行設定也必須綁
		 * 這個 unbind 沒有使用的場合
		 */
		//void unbind() const
		//{
		//	glBindVertexArray( 0 );
		//}

		bool isValid() const
		{
			return _id != 0;
		}

	private:

		std::shared_ptr<::gl::Core> _core;

		void release()
		{
			if ( _EBO != nullptr )
			{
				_EBO = nullptr;
			}

			if ( _id != 0 )
			{
				// 釋放 VAO 資源，曾啟用的屬性也都會自動清除
				glDeleteVertexArrays( 1, &_id );
				_id = 0;
			}
		}

		GLuint _id = 0;

		struct AttribNode
		{
			gl::VertexAttrib *ptr = nullptr;
			GLuint index = 0;
		};

		std::vector<::gl::VertexArray::AttribNode>  _attribList;
		std::shared_ptr<gl::ElementsBuffer> _EBO = nullptr;

	public:

		GLuint _getID() const
		{
			return _id;
		}

		// 給 gl::VertexAttrib 使用的，用來記住有什麼屬性來申請過
		void _addAttrib( GLuint index, gl::VertexAttrib* ptr )
		{
			// 檢查是否已有相同的 index
			for ( const auto& node : _attribList )
			{
				// 發現已經被佔用的 index
				if ( node.index == index )
				{
					/*
					 * 這個 index 已經被使用過了，不能再使用
					 * 由於本專案注重的是示範程式碼的可讀性
					 * 並不講究架構
					 * 所以這裡就直接拋 exception 了
					 */
					throw std::runtime_error( "gl::VertexArray::addAttrib() - A VertexAttrib with the same index already exists. Please check your code." );
				}
			}

			struct AttribNode   node;

			node.ptr = ptr;
			node.index = index;

			_attribList.push_back( node );
		}
};

}

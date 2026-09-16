#pragma once

#include <glad/glad.h>

namespace gl{

// 包裝 VAO（Vertex Array Object）的 DSA 操作
// 使用 RAII 管理生命週期，禁止複製、允許移動
class VertexArray
{
    public:

        VertexArray()
        {
            glCreateVertexArrays( 1, &_id );
        }

        ~VertexArray()
        {
            release();
        }

        // 禁用複製
        VertexArray( const VertexArray& ) = delete;
        VertexArray& operator=( const VertexArray& ) = delete;

        // 允許移動
        VertexArray( VertexArray&& rhs ) noexcept
            : _id( rhs._id )
        {
            rhs._id = 0;
        }

        VertexArray& operator=( VertexArray&& rhs ) noexcept
        {
            if ( this != &rhs )
            {
                release();
                _id     = rhs._id;
                rhs._id = 0;
            }

            return *this;
        }

        // 啟用指定索引的頂點屬性
        void enableAttrib( GLuint index )
        {
            glEnableVertexArrayAttrib( _id, index );
        }

        // 停用指定索引的頂點屬性
        void disableAttrib( GLuint index )
        {
            glDisableVertexArrayAttrib( _id, index );
        }

        // 設定屬性的資料格式（浮點數版本）
        void setAttribFormat(
            GLuint index, GLint size, GLenum type,
            GLboolean normalized, GLuint relativeOffset )
        {
            glVertexArrayAttribFormat( _id, index, size, type, normalized, relativeOffset );
        }

        // 設定屬性的資料格式（整數版本，例如 GL_INT / GL_UNSIGNED_BYTE 不需要正規化時使用）
        void setAttribIFormat(
            GLuint index, GLint size, GLenum type, GLuint relativeOffset )
        {
            glVertexArrayAttribIFormat( _id, index, size, type, relativeOffset );
        }

        // 將指定屬性索引連結到指定的 binding point
        void setAttribBinding( GLuint index, GLuint bindingIndex )
        {
            glVertexArrayAttribBinding( _id, index, bindingIndex );
        }

        // 設定每個 binding point 之間的實例更新頻率（instancing 用，預設為 0 表示逐頂點）
        void setBindingDivisor( GLuint bindingIndex, GLuint divisor )
        {
            glVertexArrayBindingDivisor( _id, bindingIndex, divisor );
        }

        // 將指定的頂點緩衝區綁定到指定的 binding point
        // buffer：來源 VBO 的 id
        // offset：緩衝區內的起始偏移量（bytes）
        // stride：每個頂點所佔的 bytes 數
        void bindVertexBuffer( GLuint bindingIndex, GLuint buffer, GLintptr offset, GLsizei stride )
        {
            glVertexArrayVertexBuffer( _id, bindingIndex, buffer, offset, stride );
        }

        // 綁定 Element Buffer（IBO / EBO）
        void bindElementBuffer( GLuint buffer )
        {
            glVertexArrayElementBuffer( _id, buffer );
        }

        // 綁定此 VAO 為目前使用的頂點陣列
        void bind() const
        {
            glBindVertexArray( _id );
        }

        // 解除綁定（切換回預設的 0 號 VAO）
        void unbind() const
        {
            glBindVertexArray( 0 );
        }

        bool isValid() const
        {
            return _id != 0;
        }

        GLuint getID() const
        {
            return _id;
        }

    private:

        void release()
        {
            if ( _id != 0 )
            {
                glDeleteVertexArrays( 1, &_id );
                _id = 0;
            }
        }

        GLuint _id = 0;
};

}
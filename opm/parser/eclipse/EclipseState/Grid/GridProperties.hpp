/*
  Copyright 2014 Statoil ASA.

  This file is part of the Open Porous Media project (OPM).

  OPM is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  OPM is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with OPM.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef ECLIPSE_GRIDPROPERTIES_HPP_
#define ECLIPSE_GRIDPROPERTIES_HPP_

#include <set>
#include <string>
#include <vector>
#include <unordered_map>

#include <opm/common/OpmLog/OpmLog.hpp>

#include <opm/parser/eclipse/EclipseState/Grid/EclipseGrid.hpp>
#include <opm/parser/eclipse/EclipseState/Grid/GridProperty.hpp>
#include <opm/parser/eclipse/Parser/MessageContainer.hpp>

/*
  This class implements a container (std::unordered_map<std::string ,
  Gridproperty<T>>) of Gridproperties. Usage is as follows:

    1. Instantiate the class; passing the number of grid cells and the
       supported keywords as a list of strings to the constructor.

    2. Query the container with the supportsKeyword() and hasKeyword()
       methods.

    3. When you ask the container to get a keyword with the
       getKeyword() method it will automatically create a new
       GridProperty object if the container does not have this
       property.
*/


namespace Opm {

    class Eclipse3DProperties;

    template <typename T>
    class GridProperties {
    public:
        typedef typename GridProperty<T>::SupportedKeywordInfo SupportedKeywordInfo;

        GridProperties(const EclipseGrid& eclipseGrid,
                       std::vector< SupportedKeywordInfo >&& supportedKeywords) :
            m_eclipseGrid( eclipseGrid )
        {
            for (auto iter = supportedKeywords.begin(); iter != supportedKeywords.end(); ++iter)
                m_supportedKeywords.emplace( iter->getKeywordName(), std::move( *iter ) );
        }

        bool supportsKeyword(const std::string& keyword) const {
            return m_supportedKeywords.count( keyword ) > 0;
        }

        bool hasKeyword(const std::string& keyword) const {
            const auto cnt = m_properties.count( keyword );
            const bool positive = cnt > 0;

            return positive && !isAutoGenerated_(keyword);
        }

        size_t size() const {
            return m_property_list.size();
        }

        const GridProperty<T>& getKeyword(const std::string& keyword) const {
            if (!hasKeyword(keyword))
                addAutoGeneratedKeyword_(keyword);

            return *m_properties.at( keyword );
        }

        GridProperty<T>& getKeyword(const std::string& keyword) {
            if (!hasKeyword(keyword))
                addAutoGeneratedKeyword_(keyword);

            return *m_properties.at( keyword );
        }

        const GridProperty<T>& getKeyword(size_t index) const {
            if (index < size())
                return *m_property_list[index];
            else
                throw std::invalid_argument("Invalid index");
        }

        GridProperty<T>& getKeyword(size_t index)  {
            if (index < size())
                return *m_property_list[index];
            else
                throw std::invalid_argument( "Invalid index" );
        }




        const GridProperty<T>& getInitializedKeyword(const std::string& keyword) const {
            if (hasKeyword(keyword))
                return *m_properties.at( keyword );
            else {
                if (supportsKeyword(keyword))
                    throw std::invalid_argument("Keyword: " + keyword + " is supported - but not initialized.");
                else
                    throw std::invalid_argument("Keyword: " + keyword + " is not supported.");
            }
        }

        bool addKeyword(const std::string& keywordName) {
            if (!supportsKeyword( keywordName ))
                throw std::invalid_argument("The keyword: " + keywordName + " is not supported in this container");

            if (hasKeyword(keywordName))
                return false;
            else {
                // if the property was already added auto generated, we just need to make it
                // non-auto generated
                if (m_autoGeneratedProperties_.count(keywordName)) {
                    OpmLog::addMessage(Log::MessageType::Warning,
                                       "The keyword "+keywordName+" has been used to calculate the "
                                       "defaults of another keyword before the first time it was "
                                       "explicitly mentioned in the deck. Maybe you need to change "
                                       "the ordering of your keywords (move "+keywordName+" to the "
                                       "front?).");
                    m_autoGeneratedProperties_.erase(m_autoGeneratedProperties_.find(keywordName));
                    return true;
                }

                auto supportedKeyword = m_supportedKeywords.at( keywordName );
                int nx = m_eclipseGrid.getNX();
                int ny = m_eclipseGrid.getNY();
                int nz = m_eclipseGrid.getNZ();
                std::shared_ptr<GridProperty<T> > newProperty(new GridProperty<T>(nx , ny , nz , supportedKeyword));

                m_properties.insert( std::pair<std::string , std::shared_ptr<GridProperty<T> > > ( keywordName , newProperty ));
                m_property_list.push_back( newProperty );
                return true;
            }
        }

        void copyKeyword(const std::string& srcField ,
                         const std::string& targetField ,
                         const Box& inputBox) {
            const auto& src = this->getKeyword( srcField );
            auto& target    = this->getOrCreateProperty( targetField );

            target.copyFrom( src , inputBox );
        }


        const MessageContainer& getMessageContainer() const {
            return m_messages;
        }

        
        MessageContainer& getMessageContainer() {
            return m_messages;
        }


        template <class Keyword>
        bool hasKeyword() const {
            return hasKeyword( Keyword::keywordName );
        }

        template <class Keyword>
        const GridProperty<T>& getKeyword() const {
            return getKeyword( Keyword::keywordName );
        }

        template <class Keyword>
        const GridProperty<T>& getInitializedKeyword() const {
            return getInitializedKeyword( Keyword::keywordName );
        }

        GridProperty<T>& getOrCreateProperty(const std::string name) {
            if (!hasKeyword(name)) {
                addKeyword(name);
            }
            return getKeyword(name);
        }

    private:
        /// this method exists for (friend) Eclipse3DProperties to be allowed initializing PORV keyword
        void postAddKeyword(const std::string& name,
                            const T defaultValue,
                            GridPropertyPostFunction< T >& postProcessor,
                            const std::string& dimString )
            {
                m_supportedKeywords.emplace(name,
                                            SupportedKeywordInfo( name,
                                                                  defaultValue,
                                                                  postProcessor,
                                                                  dimString ));
            }

        bool addAutoGeneratedKeyword_(const std::string& keywordName) const {
            if (!supportsKeyword( keywordName ))
                throw std::invalid_argument("The keyword: " + keywordName + " is not supported in this container");

            if (m_properties.count( keywordName ) > 0)
                return false; // property already exists (if it is auto generated or not doesn't matter)
            else {
                auto& supportedKeyword = m_supportedKeywords.at( keywordName );
                int nx = m_eclipseGrid.getNX();
                int ny = m_eclipseGrid.getNY();
                int nz = m_eclipseGrid.getNZ();
                std::shared_ptr<GridProperty<T> > newProperty(new GridProperty<T>(nx , ny , nz , supportedKeyword));

                m_autoGeneratedProperties_.insert(keywordName);

                m_properties.insert( std::pair<std::string , std::shared_ptr<GridProperty<T> > > ( keywordName , newProperty ));
                m_property_list.push_back( newProperty );
                return true;
            }
        }

        bool isAutoGenerated_(const std::string& keyword) const {
            return m_autoGeneratedProperties_.count(keyword);
        }

        friend class Eclipse3DProperties; // needed for PORV keyword entanglement
        const EclipseGrid& m_eclipseGrid;
        MessageContainer m_messages;
        std::unordered_map<std::string, SupportedKeywordInfo> m_supportedKeywords;
        mutable std::map<std::string , std::shared_ptr<GridProperty<T> > > m_properties;
        mutable std::set<std::string> m_autoGeneratedProperties_;
        mutable std::vector<std::shared_ptr<GridProperty<T> > > m_property_list;
    };
}

#endif // ECLIPSE_GRIDPROPERTIES_HPP_
